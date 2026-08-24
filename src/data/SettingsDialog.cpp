#include "SettingsDialog.h"

#include "../app/AdjustmentCoordinator.h"
#include "../app/VibranceService.h"
#include "../ddc/MonitorManager.h"
#include "../utils/AutostartUtils.h"
#include "../utils/resource.h"
#include "Settings.h"

#include <commctrl.h>

namespace
{
    // Slider position shown when all-monitors channels disagree (UI spec
    // section 4: "neutral state"). Moving the slider then writes that
    // position everywhere, which is the natural resolution.
    constexpr int kNeutralSliderPos = 50;

    // Detection poll: runs ONLY while the coordinator's first enumeration
    // (typically a couple of seconds of blocking DDC I/O) is outstanding,
    // then kills itself permanently - this does not violate the M8 rule
    // against fast ongoing polling.
    constexpr UINT_PTR kDetectTimerId = 1;
    constexpr unsigned kDetectIntervalMs = 400;

    const wchar_t* const kDetectingText = L"detecting...";
    const wchar_t* const kUnsupportedText = L"not supported on this monitor";
    // M13 vibrance row when no NVIDIA driver interface is present.
    const wchar_t* const kUnavailableText = L"unavailable (needs NVIDIA GPU)";

    /// SettingsData field holding the "last applied value" for |param|
    /// (slider-drag persistence).
    int& LastValueSlot(Param param)
    {
        SettingsData& data = Settings::instance().data();
        switch (param)
        {
        case Param::Brightness:
            return data.lastBrightness;
        case Param::Contrast:
            return data.lastContrast;
        case Param::RedGain:
            return data.lastRed;
        case Param::GreenGain:
            return data.lastGreen;
        case Param::BlueGain:
            return data.lastBlue;
        case Param::Saturation:
            return data.lastSaturation;
        case Param::Volume:
            return data.lastVolume;
        default:
            return data.lastSaturation;
        }
    }

    struct RowControls
    {
        Param param;
        int checkbox;
        int trackbar;
        int label;
    };

    constexpr RowControls kRows[kParamCount] = {
        // Visual order (matches the .rc template): the three core DDC
        // parameters first, then the M13/M14 additions.
        { Param::Brightness, IDC_CHK_BRIGHTNESS, IDC_TRK_BRIGHTNESS, IDC_LBL_BRIGHTNESS },
        { Param::Contrast, IDC_CHK_CONTRAST, IDC_TRK_CONTRAST, IDC_LBL_CONTRAST },
        { Param::Saturation, IDC_CHK_SATURATION, IDC_TRK_SATURATION, IDC_LBL_SATURATION },
        // R/G/B gains are slider-only: checkbox id 0 = no scroll control
        // (the scroll gesture must never shift color balance, M12).
        { Param::RedGain, 0, IDC_TRK_RED, IDC_LBL_RED },
        { Param::GreenGain, 0, IDC_TRK_GREEN, IDC_LBL_GREEN },
        { Param::BlueGain, 0, IDC_TRK_BLUE, IDC_LBL_BLUE },
        // M14 volume: slider-only (checkbox id 0), human decision matching
        // the R/G/B rows - scrolling never touches loudness either.
        { Param::Volume, 0, IDC_TRK_VOLUME, IDC_LBL_VOLUME },
    };

    const wchar_t* MonitorDisplayName(const MonitorManager& monitors, size_t index)
    {
        const auto& entries = monitors.Monitors();
        if (index >= entries.size() || entries[index].physical.empty())
        {
            return nullptr;
        }
        // Precedence per the PowerDisplay study (docs/07): EDID-derived QDC
        // friendly name first, driver description second - each guarded
        // against Windows placeholders like "Generic PnP Monitor".
        if (!MonitorManager::IsGenericDisplayName(entries[index].friendlyName.c_str()))
        {
            return entries[index].friendlyName.c_str();
        }
        const wchar_t* desc = entries[index].physical.front().handle.Description();
        if (!MonitorManager::IsGenericDisplayName(desc))
        {
            return desc;
        }
        return nullptr;
    }
}

SettingsDialog::~SettingsDialog()
{
    // Flush any un-saved slider memories before the window goes away.
    Settings::instance().SaveIfDirty();
    if (m_hwnd)
    {
        // Modeless dialogs die with DestroyWindow, never EndDialog.
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void SettingsDialog::Open(HINSTANCE hInstance, AdjustmentCoordinator* coordinator,
                          MonitorManager* monitors, VibranceService* vibrance)
{
    m_instance = hInstance;
    m_coordinator = coordinator;
    m_monitors = monitors;
    m_vibrance = vibrance;

    if (!m_hwnd)
    {
        m_hwnd = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr,
                                    &SettingsDialog::DlgProc, reinterpret_cast<LPARAM>(this));
        if (!m_hwnd)
        {
            return;
        }
    }

    // Refresh against current coordinator state every time it surfaces:
    // scroll gestures or menu toggles may have changed things while hidden.
    SendMessageW(m_hwnd, WM_APP, 0, 0);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

bool SettingsDialog::IsDialogInput(MSG* msg) const
{
    // Visibility guard is load-bearing: the dialog is HIDDEN rather than
    // destroyed on close, and IsDialogMessageW would otherwise keep
    // consuming Tab/Esc/arrow keys for an invisible window forever.
    return m_hwnd && IsWindowVisible(m_hwnd) && IsDialogMessageW(m_hwnd, msg) != FALSE;
}

void SettingsDialog::OnMonitorsChanged()
{
    if (!m_hwnd)
    {
        return;
    }
    RefreshScopeCombo();
    RefreshRows();
}

INT_PTR CALLBACK SettingsDialog::DlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Modeless creation delivers WM_INITDIALOG with the CreateDialogParam
    // lParam; afterwards GWLP_USERDATA carries the instance.
    SettingsDialog* self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
    if (msg == WM_INITDIALOG)
    {
        self = reinterpret_cast<SettingsDialog*>(lParam);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = dlg;
        self->OnInitDialog();
        return TRUE;
    }

    if (!self)
    {
        return FALSE;
    }

    switch (msg)
    {
    case WM_APP: // "surface" refresh request from Open()
        self->RefreshRows();
        return TRUE;

    case WM_TIMER:
        if (wParam == kDetectTimerId)
        {
            // Early-open race: dialog was created before the worker finished
            // enumerating monitors, so rows showed "detecting..." and the
            // scope dropdown had no monitor entries yet. Once detection lands,
            // stop polling forever and snap to the real capability state.
            if (self->m_coordinator->Ready())
            {
                KillTimer(dlg, kDetectTimerId);
                self->RefreshScopeCombo();
                self->RefreshRows();
            }
            else
            {
                self->RefreshRows(); // stays in "detecting..." state
            }
            return TRUE;
        }
        break;

    case WM_HSCROLL:
    {
        // TrackBar notifications only (lParam == control handle). The label
        // updates instantly; the DDC write goes through the coordinator's
        // debounce path - dragging fast just replaces pending targets.
        // Slider positions are persisted too, but DRAGS only mark dirty:
        // saving on every tick would hammer the disk; the save happens when
        // the dialog hides or the app exits (SaveIfDirty).
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (!control)
        {
            break;
        }
        // M11: TB_THUMBTRACK/TB_ENDTRACK bracket a live thumb drag; between
        // them the row belongs to the user and external refreshes skip it.
        const int notification = HIWORD(wParam);

        // M13: the vibrance row routes to the GPU driver, not the DDC
        // coordinator - handled before the kRows loop so its control id is
        // never mistaken for a Param trackbar.
        if (GetDlgCtrlID(control) == IDC_TRK_VIBRANCE)
        {
            if (notification == TB_THUMBTRACK)
            {
                self->m_draggingVibrance = true;
            }
            else if (notification == TB_ENDTRACK)
            {
                self->m_draggingVibrance = false;
            }
            const int pos = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
            SetDlgItemInt(dlg, IDC_LBL_VIBRANCE, static_cast<UINT>(pos), FALSE);
            self->m_vibrance->SetLevel(pos);
            Settings::instance().data().lastVibrance = pos;
            Settings::instance().MarkDirty();
            return TRUE;
        }

        for (const RowControls& row : kRows)
        {
            if (GetDlgCtrlID(control) == row.trackbar)
            {
                if (notification == TB_THUMBTRACK)
                {
                    self->m_dragging[static_cast<int>(row.param)] = true;
                }
                else if (notification == TB_ENDTRACK)
                {
                    self->m_dragging[static_cast<int>(row.param)] = false;
                }
                const int pos = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
                SetDlgItemInt(dlg, row.label, static_cast<UINT>(pos), FALSE);
                self->m_coordinator->AdjustTo(row.param, pos);
                LastValueSlot(row.param) = pos;
                Settings::instance().MarkDirty();
                break;
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_CHK_BRIGHTNESS:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool checked = IsDlgButtonChecked(dlg, IDC_CHK_BRIGHTNESS) == BST_CHECKED;
                self->m_coordinator->SetParamEnabled(Param::Brightness, checked);
                Settings::instance().data().brightnessEnabled = checked;
                Settings::instance().Save();
            }
            return TRUE;

        case IDC_CHK_CONTRAST:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool checked = IsDlgButtonChecked(dlg, IDC_CHK_CONTRAST) == BST_CHECKED;
                self->m_coordinator->SetParamEnabled(Param::Contrast, checked);
                Settings::instance().data().contrastEnabled = checked;
                Settings::instance().Save();
            }
            return TRUE;

        case IDC_CHK_SATURATION:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool checked = IsDlgButtonChecked(dlg, IDC_CHK_SATURATION) == BST_CHECKED;
                self->m_coordinator->SetParamEnabled(Param::Saturation, checked);
                Settings::instance().data().saturationEnabled = checked;
                Settings::instance().Save();
            }
            return TRUE;

        case IDC_CHK_AUTOSTART:
            // Autostart is inherently persisted state (the tray menu item
            // already writes through immediately), so this checkbox applies
            // right away too - the registry IS the persisted flag; nothing
            // extra goes into settings.json.
            if (HIWORD(wParam) == BN_CLICKED)
            {
                Autostart::SetEnabled(IsDlgButtonChecked(dlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
            }
            return TRUE;

        case IDC_CMB_SCOPE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                const int sel = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
                // Index 0 = "All monitors", index i>0 = monitor i-1.
                const int scope = sel > 0 ? sel - 1 : -1;
                self->m_coordinator->SetScope(scope);
                Settings::instance().data().scope = scope;
                Settings::instance().Save();
                self->RefreshRows();
            }
            return TRUE;

        case IDC_CMB_STEP:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                wchar_t text[16]{};
                const int sel = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
                if (sel >= 0 &&
                    SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text)) > 0)
                {
                    const int step = _wtoi(text);
                    self->m_coordinator->SetScrollStep(step);
                    Settings::instance().data().scrollStep = step;
                    Settings::instance().Save();
                }
            }
            return TRUE;

        case IDC_BTN_CLOSE:
        case IDCANCEL: // Esc key lands here in a modeless dialog
            Settings::instance().SaveIfDirty(); // flush slider-drag memories
            ShowWindow(dlg, SW_HIDE);
            return TRUE;

        default:
            break;
        }
        break;

    case WM_CLOSE: // title bar X: hide, never destroy (UI spec section 5)
        Settings::instance().SaveIfDirty();
        ShowWindow(dlg, SW_HIDE);
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

void SettingsDialog::OnInitDialog()
{
    RefreshScopeCombo();

    // Scroll step choices (default 2 matches kDefaultScrollStep).
    const HWND steps = GetDlgItem(m_hwnd, IDC_CMB_STEP);
    for (const int step : { 1, 2, 3, 5, 10 })
    {
        wchar_t text[16];
        swprintf_s(text, L"%d", step);
        SendMessageW(steps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    const int currentStep = m_coordinator->ScrollStep();
    int stepSel = 1; // list order maps 0..4 -> 1,2,3,5,10; default "2"
    for (int i = 0; i < 5; ++i)
    {
        constexpr int kChoices[5] = { 1, 2, 3, 5, 10 };
        if (kChoices[i] == currentStep)
        {
            stepSel = i;
            break;
        }
    }
    SendMessageW(steps, CB_SETCURSEL, static_cast<WPARAM>(stepSel), 0);

    // Scroll-gesture checkboxes (gates scrolling ONLY, per UI spec).
    // Slider-only rows (checkbox id 0) have nothing to initialize.
    for (const RowControls& row : kRows)
    {
        if (row.checkbox)
        {
            CheckDlgButton(m_hwnd, row.checkbox,
                           m_coordinator->IsParamEnabled(row.param) ? BST_CHECKED : BST_UNCHECKED);
        }
    }

    // Autostart reflects live registry state, same source as the tray menu.
    CheckDlgButton(m_hwnd, IDC_CHK_AUTOSTART, Autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);

    for (const RowControls& row : kRows)
    {
        const HWND trackbar = GetDlgItem(m_hwnd, row.trackbar);
        SendMessageW(trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(trackbar, TBM_SETPAGESIZE, 0, 5);
    }

    // M13: vibrance uses the device-reported range (probe evidence: 0-100
    // with 50 neutral - same percent scale as the DDC rows).
    const HWND vibranceTrackbar = GetDlgItem(m_hwnd, IDC_TRK_VIBRANCE);
    SendMessageW(vibranceTrackbar, TBM_SETRANGE, TRUE,
                 MAKELPARAM(m_vibrance->MinLevel(), m_vibrance->MaxLevel()));
    SendMessageW(vibranceTrackbar, TBM_SETPAGESIZE, 0, 5);

    RefreshRows();

    // Opened before background detection finished? Poll until it lands
    // (see WM_TIMER); the timer kills itself on first Ready() tick.
    if (!m_coordinator->Ready())
    {
        SetTimer(m_hwnd, kDetectTimerId, kDetectIntervalMs, nullptr);
    }
}

void SettingsDialog::RefreshScopeCombo()
{
    // Monitor scope: entry 0 is always "All monitors"; then one entry per
    // logical monitor. Names prefer the EDID-derived QDC friendly name,
    // fall back to the driver description, then to "Monitor N" (docs/07).
    const HWND combo = GetDlgItem(m_hwnd, IDC_CMB_SCOPE);
    const int previous = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All monitors"));
    size_t index = 0;
    size_t itemCount = 1;
    wchar_t fallback[64];
    while (true)
    {
        const wchar_t* name = MonitorDisplayName(*m_monitors, index);
        if (!name && index >= m_monitors->Monitors().size())
        {
            break;
        }
        if (!name)
        {
            swprintf_s(fallback, L"Monitor %zu", index + 1);
            name = fallback;
        }
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        ++index;
        ++itemCount;
    }

    int select = m_coordinator->Scope();
    if (previous >= 0 && static_cast<size_t>(previous) < itemCount)
    {
        select = previous - 1; // keep whatever the user had picked
    }
    // M6: after hot-plug the persisted index may point past the (new) end
    // of the monitor list - it would silently match nothing forever. Reset
    // to "All monitors" instead, and persist that so a relaunch agrees.
    if (select >= 0 && static_cast<size_t>(select) > itemCount - 2)
    {
        m_coordinator->SetScope(-1);
        Settings::instance().data().scope = -1;
        Settings::instance().Save();
        select = -1;
    }
    SendMessageW(combo, CB_SETCURSEL, select < 0 ? 0 : static_cast<WPARAM>(select + 1), 0);
}

void SettingsDialog::OnValuesChanged()
{
    // Live sync (M11): tray scrolling landed while this window is open.
    // RefreshRows reads the coordinator's cached values, which AdjustAll
    // updated synchronously on this same UI thread - zero lag, no timers,
    // no cross-thread marshaling needed. Rows mid-drag are skipped inside
    // RefreshRows; a hidden dialog needs nothing.
    if (!m_hwnd || !IsWindowVisible(m_hwnd))
    {
        return;
    }
    RefreshRows();
}

void SettingsDialog::RefreshRows()
{
    const int scope = m_coordinator->Scope();

    for (const RowControls& row : kRows)
    {
        // checkbox id 0 = slider-only row (R/G/B gains): GetDlgItem would
        // return the dialog itself for id 0, so guard every use.
        const HWND checkbox = row.checkbox ? GetDlgItem(m_hwnd, row.checkbox) : nullptr;
        const HWND trackbar = GetDlgItem(m_hwnd, row.trackbar);

        if (!m_coordinator->Ready())
        {
            // Detection still running: nothing is known yet, so "not
            // supported" would be a LIE (and stuck, pre-readiness-fix).
            // Gray everything with an honest reason; the WM_TIMER path
            // refreshes this within a few hundred ms of channels landing.
            if (checkbox)
            {
                EnableWindow(checkbox, FALSE);
            }
            EnableWindow(trackbar, FALSE);
            SetDlgItemTextW(m_hwnd, row.label, kDetectingText);
            continue;
        }

        if (!m_coordinator->SupportsParam(row.param, scope))
        {
            // Expected-state handling per UI spec section 3 / FR9: gray the
            // whole ROW out and say why - never hide it.
            if (checkbox)
            {
                EnableWindow(checkbox, FALSE);
            }
            EnableWindow(trackbar, FALSE);
            SetDlgItemTextW(m_hwnd, row.label, kUnsupportedText);
            continue;
        }

        if (checkbox)
        {
            EnableWindow(checkbox, TRUE);
        }
        EnableWindow(trackbar, TRUE);

        if (!m_dragging[static_cast<int>(row.param)])
        {
            const int value = m_coordinator->GetValue(row.param, scope);
            const int pos = value < 0 ? kNeutralSliderPos : value; // mixed -> neutral
            SendMessageW(trackbar, TBM_SETPOS, TRUE, static_cast<LPARAM>(pos));

            if (value == AdjustmentCoordinator::kValueMixed)
            {
                SetDlgItemTextW(m_hwnd, row.label, L"varies");
            }
            else
            {
                SetDlgItemInt(m_hwnd, row.label, static_cast<UINT>(pos), FALSE);
            }
        }
    }

    // M13: GPU vibrance row - independent of coordinator readiness (the GPU
    // answers instantly; there is no detection phase to wait out) and of the
    // monitor scope. Same never-hide rule: absent hardware grays with text.
    {
        const HWND vibranceTrackbar = GetDlgItem(m_hwnd, IDC_TRK_VIBRANCE);
        const int level = m_vibrance ? m_vibrance->GetLevel() : -1;
        if (!m_vibrance || !m_vibrance->Available() || level < 0)
        {
            EnableWindow(vibranceTrackbar, FALSE);
            SetDlgItemTextW(m_hwnd, IDC_LBL_VIBRANCE,
                            m_vibrance && m_vibrance->Available() ? kDetectingText : kUnavailableText);
        }
        else if (!m_draggingVibrance)
        {
            EnableWindow(vibranceTrackbar, TRUE);
            SendMessageW(vibranceTrackbar, TBM_SETPOS, TRUE, static_cast<LPARAM>(level));
            SetDlgItemInt(m_hwnd, IDC_LBL_VIBRANCE, static_cast<UINT>(level), FALSE);
        }
    }
}
