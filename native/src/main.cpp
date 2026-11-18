#include <windows.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    MessageBoxW(nullptr,
        L"LegacyMenuBridge is installed.\n\n"
        L"Right-click a file or folder and choose \"Classic Menu\" "
        L"in the Windows 11 context menu.",
        L"LegacyMenuBridge", MB_OK | MB_ICONINFORMATION);
    return 0;
}
