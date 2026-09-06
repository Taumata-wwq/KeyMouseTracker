// 开机自启动：管理员权限程序不能走 HKCU Run（登录时 UAC 拦截，启动静默失败），
// 改用计划任务（登录触发器 + 最高权限，Task Scheduler 启动不受 UAC 拦截）。
#include "autostart.h"
#include "data.h"
#include <vector>

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kTaskName = L"KeyMouseTracker";

std::wstring GetExePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    return std::wstring(buf.data(), n);
}

// 清理旧的 Run 键自启残留（新机制为计划任务，两者互斥避免双启动）
static void clearRunKey() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kAppName);
        RegCloseKey(key);
    }
}

static bool runSchtasks(const std::wstring& args, DWORD* exitCodeOut = nullptr) {
    wchar_t sysDir[MAX_PATH];
    if (!GetSystemDirectoryW(sysDir, MAX_PATH)) return false;
    std::wstring exe = std::wstring(sysDir) + L"\\schtasks.exe";
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCodeOut) *exitCodeOut = code;
    return code == 0;
}

// 自启状态缓存：IsAutoStart() 每次要 spawn schtasks.exe 查询计划任务（约 200ms），
// 而 UI 刷新（pushStats）每 500ms 就会读一次自启状态。若每次都现查，刷新一次
// 就卡 200ms+。状态只在 SetAutoStart() 切换时变化，故缓存后刷新零开销。
// -1 = 未查询；0 = 关闭；1 = 开启
static int g_autoStartCached = -1;

bool SetAutoStart(bool enable) {
    clearRunKey();
    std::wstring tn = kTaskName;
    bool ok;
    if (enable) {
        std::wstring tr = L"\"" + GetExePath() + L"\"";
        ok = runSchtasks(L"/create /tn " + tn + L" /tr " + tr +
                         L" /sc onlogon /rl highest /f");
    } else {
        ok = runSchtasks(L"/delete /tn " + tn + L" /f");
    }
    if (ok) g_autoStartCached = enable ? 1 : 0;
    return ok;
}

bool IsAutoStart() {
    if (g_autoStartCached >= 0) return g_autoStartCached != 0;
    DWORD code = 1;
    runSchtasks(L"/query /tn " + std::wstring(kTaskName), &code);
    g_autoStartCached = (code == 0) ? 1 : 0;
    return g_autoStartCached != 0;
}
