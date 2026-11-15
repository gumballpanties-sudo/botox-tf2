#pragma once
// Directory junction (NTFS mount point): no admin needed, RemoveDirectoryA on it removes the link
// only, never the target. multihud uses it for script huds (multihud.cpp EnsureScriptLinks), the
// gate test (tests/multihud_test.cpp) makes one for real and resolves a ".." climb through it.
#include <windows.h>
#include <winioctl.h>   // FSCTL_SET_REPARSE_POINT
#include <cstring>
#include <string>
#include <vector>

inline bool JunctionIsReparse(const std::string& p)
{
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// link (must not exist) -> target (absolute, existing folder)
inline bool MakeJunction(const std::string& link, const std::string& target)
{
    if (!CreateDirectoryA(link.c_str(), nullptr)) return false;
    const HANDLE h = CreateFileA(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryA(link.c_str()); return false; }
    wchar_t wt[MAX_PATH];
    const int n = MultiByteToWideChar(CP_ACP, 0, target.c_str(), -1, wt, MAX_PATH);
    bool ok = n > 0;
    if (ok)
    {
        const std::wstring print = wt, sub = L"\\??\\" + print;
        // REPARSE_DATA_BUFFER / MountPointReparseBuffer (ntifs.h, not in the user mode headers):
        // tag, data length, reserved, then 4 WORDs of offsets/lengths and the two names, NUL ended
        std::vector<BYTE> buf(8 + 8 + (sub.size() + 1 + print.size() + 1) * sizeof(wchar_t));
        auto w16 = [&](size_t off, size_t v) { const WORD x = (WORD)v; memcpy(&buf[off], &x, 2); };
        const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        memcpy(&buf[0], &tag, 4);
        w16(4, buf.size() - 8);                          // ReparseDataLength
        w16(8, 0);                                       // SubstituteNameOffset
        w16(10, sub.size() * sizeof(wchar_t));           // SubstituteNameLength
        w16(12, (sub.size() + 1) * sizeof(wchar_t));     // PrintNameOffset
        w16(14, print.size() * sizeof(wchar_t));         // PrintNameLength
        memcpy(&buf[16], sub.c_str(), (sub.size() + 1) * sizeof(wchar_t));
        memcpy(&buf[16 + (sub.size() + 1) * sizeof(wchar_t)], print.c_str(), (print.size() + 1) * sizeof(wchar_t));
        DWORD ret = 0;
        ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), (DWORD)buf.size(), nullptr, 0, &ret, nullptr) != 0;
    }
    CloseHandle(h);
    if (!ok) RemoveDirectoryA(link.c_str());
    return ok;
}
