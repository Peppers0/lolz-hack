#include <windows.h>
#include <psapi.h>
#include <omp.h>

#include <ctime>
#include <cmath>
#include <vector>
#include <sstream>
#include <iostream>

using namespace std;

enum {
    STATUS_FIND,
    STATUS_SCAN,
    STATUS_LISTEN,
    STATUS_READY
};

static MSG msg;
static vector<BYTE> chunk;
static vector<SIZE_T> test_list;
static BOOL textUpdating = FALSE;
static HANDLE cmdthr;
static HWND hooker;
static SYSTEM_INFO sysinfo;
static WNDCLASS hookerClass;
static RAWINPUTDEVICE RawInputDevice;
static BOOL retry = FALSE;
static HWND window;
static DWORD pid;
static HANDLE process = NULL;
static BOOL run = TRUE;
static BOOL triggered = FALSE;
static FLOAT heading[2];
static vector<SIZE_T> list;
static INT delta = 0;
static BOOL beep;
static FLOAT zoom = 0;
static FLOAT zoomMin = 1000.0f;
static FLOAT zoomMax = 2250.0f + WHEEL_DELTA * 40;
static INT status = STATUS_FIND;
static CONST CHAR *statusString[] = {
    [STATUS_FIND] = "Finding game window...",
    [STATUS_SCAN] = "Scanning game process...",
    [STATUS_LISTEN] = "Listening events...",
    [STATUS_READY] = "Ready!"
};

BOOL IsProcessActive();
LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
BOOL IsZoomValue(FLOAT value);
DWORD WINAPI CommandsThread(LPVOID);
VOID UpdateText();

int main() {
    GetSystemInfo(&sysinfo);
    memset(&hookerClass, 0, sizeof(hookerClass));
    hookerClass.hInstance = GetModuleHandle(NULL);
    hookerClass.lpszClassName = "lolz-hooker";
    hookerClass.lpfnWndProc = HookProc;
    if (!RegisterClass(&hookerClass)) printf("%d\n", GetLastError());

    hooker = CreateWindow(hookerClass.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hookerClass.hInstance, 0);

    RawInputDevice.usUsagePage = 1;
    RawInputDevice.usUsage = 2;
    RawInputDevice.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK;
    RawInputDevice.hwndTarget = hooker; 
    if (!RegisterRawInputDevices(&RawInputDevice, 1, sizeof(RAWINPUTHEADER))) printf("%d\n", GetLastError());

    cmdthr = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) CommandsThread, NULL, 0, NULL);

    while (run) {
        list.clear();
        if (process) (CloseHandle(process), process = NULL);

        status = STATUS_FIND;
        UpdateText();

        while (run && (window = FindWindow(NULL, "League of Legends (TM) Client")) == NULL) Sleep(1);
        GetWindowThreadProcessId(window, &pid);
        if (process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid)) { } else { Sleep(1); continue; }

        status = STATUS_SCAN;
        UpdateText();

        Sleep(1000);

        if (!IsProcessActive()) continue;

        for (SIZE_T address = (SIZE_T) sysinfo.lpMinimumApplicationAddress; address < (SIZE_T) sysinfo.lpMaximumApplicationAddress;) {
        // for (SIZE_T address = (SIZE_T) 0; address < (SIZE_T) 0x7fffffff;) {
            MEMORY_BASIC_INFORMATION info;
            if (VirtualQueryEx(process, (LPCVOID) address, &info, sizeof(info)) != sizeof(info) || info.RegionSize > 0x7fffffff) break;
            if (info.Protect == PAGE_READWRITE && info.State == MEM_COMMIT) {
                if (chunk.capacity() < info.RegionSize) chunk.resize(info.RegionSize);
                ReadProcessMemory(process, info.BaseAddress, (LPVOID) chunk.data(), info.RegionSize, NULL);
                SIZE_T terminal = info.RegionSize - sizeof(heading);
                #pragma omp parallel for shared(list)
                for (SIZE_T i = 0; i <= terminal; ++i) {
                    memcpy(heading, chunk.data() + i, sizeof(heading));
                    if (heading[0] == 2250.0f && heading[1] == 2250.0f) {
                        list.push_back(address + i);
                    }
                }
            }
            address += info.RegionSize;
        }

        if (list.empty()) { Sleep(1); continue; }

        status = STATUS_LISTEN;
        UpdateText();

        if (beep) MessageBeep(MB_OK);

        while (run && IsProcessActive() && list.size() > 1) {
            if (retry) break;
            while (PeekMessage(&msg, hooker, 0, 0, PM_REMOVE)) DispatchMessage(&msg);
            if (run && IsProcessActive()) {
                if (triggered) {
                    triggered = FALSE;
                    test_list.clear();
                    for (SIZE_T address: list) {
                        ReadProcessMemory(process, (LPCVOID) address, heading, sizeof(heading), NULL);
                        if (IsZoomValue(heading[0]) && IsZoomValue(heading[1])) {
                        // if (heading[0] != 2250.0f && heading[1] != 2250.0f) {
                            test_list.push_back(address);
                        }
                    }
                    if (test_list.size()) {
                        list = test_list;
                    }
                }
                Sleep(1);
            } else {
                break;
            }
        }

        if (retry) { retry = FALSE; continue; }

        if (run && IsProcessActive()) ReadProcessMemory(process, (LPCVOID)list.front(), (LPVOID) &zoom, sizeof(zoom), NULL);

        status = STATUS_READY;
        UpdateText();
        if (beep) MessageBeep(MB_OK);

        while (run && IsProcessActive()) {
            while (PeekMessage(&msg, hooker, 0, 0, PM_REMOVE)) DispatchMessage(&msg);
            if (run && IsProcessActive()) {
                if (triggered) {
                    triggered = FALSE;
                    zoom =
                    heading[0] = 
                    heading[1] = fminf(fmaxf(zoom - delta, zoomMin), zoomMax);
                    UpdateText();
                }
                WriteProcessMemory(process, (LPVOID) list.front(), (LPCVOID) heading, sizeof(heading), NULL);
                Sleep(1);
            } else {
                break;
            }
        }
    }

    if (process) {
        heading[0] =
        heading[1] = 2250.0f;
        WriteProcessMemory(process, (LPVOID) list.front(), (LPCVOID) heading, sizeof(heading), NULL);
        CloseHandle(process);
    }
    DestroyWindow(hooker);
    UnregisterClass(hookerClass.lpszClassName, hookerClass.hInstance);

    TerminateThread(cmdthr, 0);

    return 0;
}

BOOL IsProcessActive() {
    if (process) {
        DWORD exitCode;
        GetExitCodeProcess(process, &exitCode);
        return exitCode == STILL_ACTIVE;
    }
    return FALSE;
}

LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT) {
        UINT dwSize;
        HRAWINPUT hRawInput = (HRAWINPUT)(lparam);
        if (GetRawInputData(hRawInput, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) != 0xffffffff) {
            LPBYTE lpb = new BYTE[dwSize];
            if (GetRawInputData(hRawInput, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) > 0) {
                RAWINPUT* praw = (RAWINPUT*)(lpb);
                if (praw->header.dwType == RIM_TYPEMOUSE) {
                    if (praw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                        POINT point;
                        GetCursorPos(&point);
                        if (GetForegroundWindow() == window && WindowFromPoint(point) == window) {
                            triggered = TRUE;
                            delta = (short)(praw->data.mouse.usButtonData);
                        }
                    }
                }
            }
            delete[] lpb;
        }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

BOOL IsZoomValue(FLOAT value) {
    static INT zoomRate = (2250 - 1000) / WHEEL_DELTA;
    for (INT i = 1; i <= zoomRate; ++i) {
        FLOAT u = 1000.0f + WHEEL_DELTA * i;
        FLOAT d = 2250.0f - WHEEL_DELTA * (11 - i);
        if (value <= u && value >= d) return TRUE;
    }
    return FALSE;
}

DWORD WINAPI CommandsThread(LPVOID) {
    while (run) {
        UpdateText();
        string command;
        getline(cin, command);
        if (command == "retry") retry = TRUE;
        else if (command == "quit") run = FALSE;
        else if (command == "beep") beep = !beep;
        else if (command == "reset") {
            zoom = 
            heading[0] =
            heading[1] = 2250.0f;
        }
    }
    return 0;
}

VOID UpdateText() {
    if (textUpdating) return; else textUpdating = TRUE;
    system("cls");
    puts("This program lets the user see in 'bigger' view in game.");
    puts("");
    printf("Status: %s\n", statusString[status]);
    if (status == STATUS_READY) {
        printf("Zoom: %.0f\n", zoom);
    } else {
        puts("Zoom: unkown yet.");
    }
    puts("");
    puts("Usage:");
    puts("Make sure if the view is fully zoomed out.");
    printf("If the status says '%s' then you should zoom in and out until results.\n", statusString[STATUS_LISTEN]);
    puts("");
    puts("Enter 'quit' for safe exit.");
    if (beep) puts("Enter 'beep' to turn OFF sound notificion.");
    else puts("Enter 'beep' to turn ON sound notificion. (System sounds must be enabled.)");
    puts("Enter 'reset' to reset zoom.");
    puts("Enter 'retry' to retry scanning.");
    puts("");
    puts("Author:\n Dresmor Alakazard");
    puts("");
    printf("Command: ");
    textUpdating = FALSE;
}