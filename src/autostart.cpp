// 开机自启动
#include "autostart.h"
#include "data.h"
#include <vector>

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

std::wstring GetExePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    return std::wstring(buf.data(), n);
}

static void clearStartupApproved() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kAppName);
        RegCloseKey(key);
    }
}

bool SetAutoStart(bool enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LSTATUS r;
    if (enable) {
        std::wstring path = L"\"" + GetExePath() + L"\"";
        r = RegSetValueExW(key, kAppName, 0, REG_SZ, (const BYTE*)path.c_str(),
                           (DWORD)((path.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) return false;
    if (enable) clearStartupApproved();
    return true;
}

bool IsAutoStart() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LSTATUS r = RegQueryValueExW(key, kAppName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}