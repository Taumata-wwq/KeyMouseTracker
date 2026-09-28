# KeyMouseTracker 外部调用 API

KeyMouseTracker 在本机提供一个 **命名管道（Named Pipe）** JSON 服务，外部软件可连接管道读取统计数据，无需解析二进制数据文件。

- 管道名：`\\.\pipe\KeyMouseTrackerApi`
- 传输：行协议（UTF-8）。客户端写入一行**命令**，服务端返回一行 **JSON**，随后关闭本次连接。
- 隐私：仅监听本机命名管道，不监听任何 TCP/UDP 端口，不联网。
- 数据读取在主线程完成（保证线程安全），响应延迟 ≤ 约 500ms。

## 1. 命令一览

| 命令 | 说明 | 响应 |
|---|---|---|
| `ping` | 存活与版本探测 | `{"ok":true,"app":"KeyMouseTracker","version":"0.4.0","lang":"zh"}` |
| `state` | 当前运行状态 | `{"paused":false,"autostart":false,"dark":true,"lang":"zh","foreApp":"explorer.exe"}` |
| `summary` | 累计总量 | 见 §2 |
| `today` | 今日统计 | 见 §3 |
| `apps` | 今日应用排行（Top 20） | 见 §4 |
| `range YYYY-MM-DD YYYY-MM-DD` | 指定日期区间汇总（闭区间） | 见 §5 |

> 命令对大小写不敏感；`range` 后两个日期以空格分隔，缺失则视为「全部历史」。

## 2. `summary` 响应

```json
{
  "days": 123,
  "keys": 123456,
  "clicks": 23456,
  "motion": 345678,
  "distCm": 1234567,
  "activeSec": 98765,
  "apps": 42
}
```

## 3. `today` 响应

```json
{
  "date": "2026-09-15",
  "keys": 1234,
  "clicks": 234,
  "left": 200, "mid": 20, "right": 14,
  "motion": 3456,
  "distCm": 12345,
  "activeSec": 3600,
  "idleSec": 7200,
  "maxSessionSec": 1800,
  "sessionCount": 12,
  "apps": 18
}
```

## 4. `apps` 响应

```json
{
  "date": "2026-09-15",
  "apps": [
    { "name": "explorer.exe", "keys": 300, "clicks": 80, "activeMin": 42 },
    { "name": "Code.exe", "keys": 200, "clicks": 60, "activeMin": 30 }
  ]
}
```

## 5. `range` 响应

```json
{
  "start": "2026-09-01",
  "end": "2026-09-15",
  "days": 15,
  "keys": 12345,
  "clicks": 2345,
  "motion": 34567,
  "distCm": 123456,
  "activeSec": 54321
}
```

## 6. 调用示例

### C++ / Win32

```cpp
#include <windows.h>
#include <cstdio>

void query(const char* cmd) {
    HANDLE h = CreateFileA("\\\\.\\pipe\\KeyMouseTrackerApi",
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { puts("not running"); return; }
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    char req[256]; snprintf(req, sizeof(req), "%s\n", cmd);
    DWORD w = 0; WriteFile(h, req, (DWORD)strlen(req), &w, nullptr);
    char buf[65536]; DWORD r = 0;
    while (ReadFile(h, buf + r, sizeof(buf) - 1 - r, &r, nullptr) && r) {}
    buf[r] = 0; puts(buf);
    CloseHandle(h);
}
```

### Python

```python
import win32file  # pip install pywin32

pipe = win32file.CreateFile(
    r"\\.\pipe\KeyMouseTrackerApi",
    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
    0, None, win32file.OPEN_EXISTING, 0, None)
win32file.SetNamedPipeHandleState(pipe, win32file.PIPE_READMODE_MESSAGE, None, None)
win32file.WriteFile(pipe, b"today\n")
_, data = win32file.ReadFile(pipe, 65536)
win32file.CloseHandle(pipe)
print(data.decode("utf-8"))
```

### PowerShell

```powershell
$p = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'KeyMouseTrackerApi', [System.IO.Pipes.PipeDirection]::InOut)
$p.Connect(2000)
$w = New-Object System.IO.StreamWriter($p); $w.WriteLine('summary'); $w.Flush()
$r = New-Object System.IO.StreamReader($p); $r.ReadLine()
$p.Close()
```

## 7. 说明

- 管道服务在应用启动时自动开启；应用退出时关闭。
- 请求为「一次连接一条命令」，服务端响应后关闭连接；如需多次查询请逐次连接（管道连接成本极低）。
- 未来可扩展：`pause` / `resume`（采集控制）等写操作，需谨慎评估并发语义后再开放。
