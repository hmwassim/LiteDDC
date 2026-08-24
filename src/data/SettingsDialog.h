#pragma once

#include "../app/AdjustmentCoordinator.h"

#include <windows.h>

class MonitorManager;
class VibranceService;

/// Modeless settings window (docs/04 sections 3-5). Exactly ONE instance
/// exists for the whole process: a tray left-click opens or restores it,
/// and every close affordance (Close button, title bar X, Esc) merely
/// HIDES it - the app keeps running from the tray and only the tray
/// menu's Exit terminates the process.
///
/// Write routing rule (roadmap non-negotiable, UI spec section 5): slider
/// drags update their numeric label instantly and push the new ABSOLUTE
/// value through AdjustmentCoordinator::AdjustTo - the same debounced,
/// worker-thread write path scrolling uses. No second write path exists.
///
/// In-memory settings live in the coordinator; nothing persists until M5.
class SettingsDialog
{
public:
    SettingsDialog() = default;
    ~SettingsDialog();

    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;

    /// Creates the dialog window on first call; afterwards shows/restores
    /// and foregrounds the existing one (single-instance guarantee).
    /// |coordinator|, |monitors| and |vibrance| must outlive this object -
    /// they are stored, not copied.
    void Open(HINSTANCE hInstance, AdjustmentCoordinator* coordinator,
              MonitorManager* monitors, VibranceService* vibrance);

    /// True once the dialog window has been created (visible or hidden).
    bool Exists() const { return m_hwnd != nullptr; }

    /// Main-loop hook: returns true if |msg| was consumed as dialog
    /// navigation input (Tab, arrows, Esc). Call before TranslateMessage.
    bool IsDialogInput(MSG* msg) const;

    /// M6: the coordinator re-enumerated (hot-plug, sleep/wake) - refresh
    /// the scope dropdown and capability rows. A persisted scope index that
    /// no longer exists (monitor unplugged, order changed) resets to
    /// "All monitors" rather than silently pointing at nothing.
    void OnMonitorsChanged();

    /// M11: values changed OUTSIDE this dialog (tray scroll while the
    /// window is open) - re-read the cached channel values into the sliders
    /// so the UI reflects adjustments live. No-op while hidden; never
    /// touches a row whose trackbar is mid-drag (that thumb belongs to the
    /// user until TB_ENDTRACK).
    void OnValuesChanged();

private:
    static INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnInitDialog();
    /// (Re)fills the monitor-scope dropdown from MonitorManager, keeping the
    /// current selection when it survives the repopulation. Called at
    /// creation and again when background detection finishes - an
    /// early-opened dialog initially only has "All monitors" to offer.
    void RefreshScopeCombo();
    /// Re-applies capability gating (gray-out + reason text vs. live
    /// slider position) for the currently selected monitor scope.
    void RefreshRows();

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    AdjustmentCoordinator* m_coordinator = nullptr;
    MonitorManager* m_monitors = nullptr;
    /// M13: GPU vibrance row's backend (may be absent-hardware; the dialog
    /// grays the row then). Deliberately NOT part of the coordinator
    /// pipeline - see VibranceService header.
    VibranceService* m_vibrance = nullptr;
    /// Per-param TB_THUMBTRACK state (M11): a row mid-drag is owned by the
    /// user; external refreshes (OnValuesChanged) must not yank the thumb.
    bool m_dragging[kParamCount] = { false, false, false };
    bool m_draggingVibrance = false;
};
