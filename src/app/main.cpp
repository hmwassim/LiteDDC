#include "App.h"

#include <windows.h>
#include <commctrl.h>

namespace
{
    constexpr wchar_t kInstanceMutexName[] = L"Local\\LiteDDC_InstanceMutex";
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    if (!mutex)
    {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        MessageBoxW(nullptr, L"LiteDDC is already running.", L"LiteDDC", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // TrackBar/ComboBox classes for the settings dialog (M4). Must run
    // before any common control is created.
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    App app(hInstance);
    if (!app.Init())
    {
        CloseHandle(mutex);
        MessageBoxW(nullptr, L"LiteDDC failed to initialize.", L"LiteDDC", MB_OK | MB_ICONERROR);
        return 1;
    }

    const int result = app.Run();
    CloseHandle(mutex);
    return result;
}
