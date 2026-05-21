#include "stdafx.h"
#include <Psapi.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, char *, int) {
    if (!AdjustCurrentPrivilege(SE_DEBUG_NAME)) {
        MessageBox(0, L"Failed to adjust privileges to debug", L"Failure", MB_OK);
        return 1;
    }

    const auto thread = CreateDialogThread();
    for (;; Sleep(200)) {
        const auto processInfo = GetProcessInfoByName(L"MirrorsEdge.exe");
        if (!processInfo.th32ProcessID) {
            continue;
        }

        const HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, false, processInfo.th32ProcessID);
        if (!process) {
            TerminateThread(thread, 0);
            MessageBox(0, L"Failed to open a handle to the process", L"Failure", 0);
            return 1;
        }

        if (HasModule(process, L"mmultiplayer.dll")) {
            CloseHandle(process);
            return 0;
        }

        TerminateThread(thread, 0);

        const auto status = LoadClient(process);
        CloseHandle(process);
        return !status;
    }
}

bool LoadClient(HANDLE process) {
    std::wstring path;
    if (!GetDllPath(path)) {
        MessageBox(0, L"Failed to get dll path", L"Failure", 0);
        return false;
    }

    // MessageBoxW(NULL, path.c_str(), L"buh", MB_OK);

    bool status = false;

    const auto size = (path.size() + 1) * sizeof(wchar_t);
    const auto arg =
        VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (arg) {
        if (WriteProcessMemory(process, arg, path.c_str(), size, nullptr)) {
            const auto thread =
                CreateRemoteThread(process, nullptr, 0,
                                   reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(
                                       GetModuleHandle(L"kernel32.dll"), "LoadLibraryW")),
                                   arg, 0, nullptr);

            if (thread) {
                WaitForSingleObject(thread, INFINITE);
                CloseHandle(thread);

                status = true;
            } else {
                MessageBox(0, L"Failed to create remote thread", L"Failure", 0);
            }
        } else {
            MessageBox(0, L"Failed to write process memory", L"Failure", 0);
        }

        VirtualFreeEx(process, arg, 0, MEM_RELEASE);
    } else {
        MessageBox(0, L"Failed to allocate virtual memory", L"Failure", 0);
    }

    return status;
}

HANDLE CreateDialogThread() {
    return CreateThread(
        nullptr, 0,
        [](void *) -> unsigned long {
            MessageBox(0, L"Waiting for Mirror's Edge to start. Click OK to stop.", L"Waiting...",
                       MB_OK);

            exit(0);
            return 0;
        },
        nullptr, 0, nullptr);
}

bool HasModule(HANDLE process, const wchar_t *module) {
    const auto snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId(process));

    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32 entry = {sizeof(entry)};
    if (Module32First(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module) == 0) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Module32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return false;
}

bool GetDllPath(std::wstring &path) {
    // https://learn.microsoft.com/en-us/cpp/text/how-to-convert-between-various-string-types?view=msvc-170
    wchar_t* filename = new wchar_t[MAX_PATH];
    HANDLE current = GetCurrentProcess();

    // Get process file name
    if (GetModuleFileNameEx(current, NULL, filename, MAX_PATH) == 0) {
        return false;
    }

    // MessageBox(0, filename, L"Debug", 0);

    wchar_t drive[_MAX_DRIVE];
    wchar_t dir[_MAX_DIR];
    wchar_t fname[_MAX_FNAME];
    wchar_t ext[_MAX_EXT];
    _wsplitpath_s(filename, drive, dir, fname, ext);

    std::wstring outpath = drive;
    outpath += dir;
    
    path = outpath + L"mmultiplayer.dll";
    return true;
}

PROCESSENTRY32 GetProcessInfoByName(const wchar_t *name) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {0};
    }

    PROCESSENTRY32 entry = {sizeof(entry)};
    if (Process32First(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                CloseHandle(snapshot);
                return entry;
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return {0};
}

bool AdjustCurrentPrivilege(const wchar_t *privilege) {
    LUID luid;
    if (!LookupPrivilegeValue(nullptr, privilege, &luid)) {
        return FALSE;
    }

    TOKEN_PRIVILEGES tp = {0};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) {
        return FALSE;
    }

    if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), nullptr, nullptr)) {
        CloseHandle(token);
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        CloseHandle(token);
        return FALSE;
    }

    CloseHandle(token);
    return TRUE;
}