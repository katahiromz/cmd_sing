// server.cpp --- cmd_sing_server.exe のソース
// Copyright (C) 2025 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <stack>
#include "../sound.h"
#include "server.h"

HINSTANCE g_hInst = NULL;
HWND g_hMainWnd = NULL;
HANDLE g_hStackMutex = NULL;

#define TIMER_ID 999 // タイマーID
#define TIMER_INTERVAL (10 * 1000)  // 10秒

#define ID_RESTART_TIMER 1000
#define ID_KILL_TIMER 1001

#define VSK_MAX_CHANNEL 6

struct VOICE_INFO
{
    INT m_ch;
    std::wstring m_file;
};

struct SERVER_CMD
{
    bool m_stopm = false;
    std::map<std::string, std::string> m_variables;
    std::string m_str_to_sing;

    int parse_cmd_line(INT argc, LPWSTR *argv);
    bool load_settings();
    bool save_settings();
};

struct SERVER
{
    bool m_stereo = true;
    bool m_no_reg = false;
    HANDLE m_hThread = NULL;
    std::stack<SERVER_CMD> m_stack;

    SERVER()
    {
    }

    ~SERVER()
    {
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    bool start();
    DWORD thread_proc();
    VSK_SOUND_ERR sing_cmd(SERVER_CMD& cmd, bool no_sound);
};

SERVER *Server_GetData(HWND hwndServer)
{
    return (SERVER *)GetWindowLongPtrW(hwndServer, GWLP_USERDATA);
}

// レジストリから設定を読み込む
bool SERVER_CMD::load_settings()
{
    // レジストリを開く
    HKEY hKey;
    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                  KEY_READ, &hKey);
    if (error)
        return false;

    // 音声の設定のサイズ
    size_t size = vsk_cmd_sing_get_setting_size();

    // 設定項目を読み込む
    std::vector<uint8_t> setting;
    setting.resize(size);
    DWORD cbValue = size;
    error = RegQueryValueExW(hKey, L"setting", NULL, NULL, setting.data(), &cbValue);
    if (!error && cbValue == size)
        vsk_cmd_sing_set_setting(setting);

    // 変数項目を読み込む
    for (DWORD dwIndex = 0; ; ++dwIndex)
    {
        CHAR szName[MAX_PATH], szValue[512];
        DWORD cchName = _countof(szName);
        DWORD cbValue = sizeof(szValue);
        error = RegEnumValueA(hKey, dwIndex, szName, &cchName, NULL, NULL, (BYTE *)szValue, &cbValue);
        szName[_countof(szName) - 1] = 0; // Avoid buffer overrun
        szValue[_countof(szValue) - 1] = 0; // Avoid buffer overrun
        if (error)
            break;

        if (std::memcmp(szName, "VAR_", 4 * sizeof(CHAR)) != 0)
            continue;

        CharUpperA(szName);
        CharUpperA(szValue);
        g_variables[&szName[4]] = szValue;
    }

    // レジストリを閉じる
    RegCloseKey(hKey);

    return true;
}

// レジストリへ設定を書き込む
bool SERVER_CMD::save_settings()
{
    // レジストリを作成
    HKEY hKey;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                    NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL);
    if (error)
        return false;

    // 音声の設定を書き込む
    std::vector<uint8_t> setting;
    vsk_cmd_sing_get_setting(setting);
    RegSetValueExW(hKey, L"setting", 0, REG_BINARY, setting.data(), (DWORD)setting.size());

    // レジストリの変数項目を消す
retry:
    for (DWORD dwIndex = 0; ; ++dwIndex)
    {
        CHAR szName[MAX_PATH];
        DWORD cchName = _countof(szName);
        error = RegEnumValueA(hKey, dwIndex, szName, &cchName, NULL, NULL, NULL, NULL);
        szName[_countof(szName) - 1] = 0; // Avoid buffer overrun
        if (error)
            break;

        if (std::memcmp(szName, "VAR_", 4 * sizeof(CHAR)) != 0)
            continue;

        RegDeleteValueA(hKey, szName);
        goto retry;
    }

    // 新しい変数項目群を書き込む
    for (auto& pair : g_variables)
    {
        std::string name = "VAR_";
        name += pair.first;
        DWORD cbValue = (pair.second.size() + 1) * sizeof(CHAR);
        RegSetValueExA(hKey, name.c_str(), 0, REG_SZ, (BYTE *)pair.second.c_str(), cbValue);
    }

    // レジストリを閉じる
    RegCloseKey(hKey);

    return true;
}

int SERVER_CMD::parse_cmd_line(INT argc, LPWSTR *argv)
{
    if (argc <= 1)
        return 0;

    for (int iarg = 1; iarg < argc; ++iarg)
    {
        LPWSTR arg = argv[iarg];

        if (arg[0] == '-' && (arg[1] == 'd' || arg[1] == 'D'))
        {
            VskString str = vsk_sjis_from_wide(&arg[2]);
            auto ich = str.find('=');
            if (ich == str.npos)
                return 1;

            auto var = str.substr(0, ich);
            auto value = str.substr(ich + 1);
            CharUpperA(&var[0]);
            CharUpperA(&value[0]);
            m_variables[var] = value;
            continue;
        }

        if (_wcsicmp(arg, L"-stopm") == 0 || _wcsicmp(arg, L"--stopm") == 0)
        {
            m_stopm = true;
            continue;
        }

        if (_wcsicmp(arg, L"-mono") == 0 || _wcsicmp(arg, L"--mono") == 0)
        {
            continue;
        }

        if (_wcsicmp(arg, L"-stereo") == 0 || _wcsicmp(arg, L"--stereo") == 0)
        {
            continue;
        }

        // hidden feature
        if (_wcsicmp(arg, L"-no-beep") == 0 || _wcsicmp(arg, L"--no-beep") == 0)
        {
            continue;
        }

        // hidden feature
        if (_wcsicmp(arg, L"-no-reg") == 0 || _wcsicmp(arg, L"--no-reg") == 0)
        {
            continue;
        }

        if (_wcsicmp(arg, L"-bgm") == 0 || _wcsicmp(arg, L"--bgm") == 0)
        {
            if (iarg + 1 < argc)
            {
                ++iarg;
                continue;
            }
            else
            {
                return 1;
            }
        }

        if (arg[0] == '-')
            return 1;

        if (m_str_to_sing.empty())
        {
            m_str_to_sing = vsk_sjis_from_wide(arg);
            continue;
        }

        return 1;
    }

    return 0;
}

VSK_SOUND_ERR SERVER::sing_cmd(SERVER_CMD& cmd, bool no_sound)
{
    return vsk_sound_cmd_sing(cmd.m_str_to_sing.c_str(), m_stereo, no_sound);
}

DWORD SERVER::thread_proc()
{
    size_t prev_size = 0;
    while (g_hMainWnd)
    {
        // 次に演奏するデータを取り出す
        SERVER_CMD cmd;
        DWORD wait = WaitForSingleObject(g_hStackMutex, INFINITE);
        if (wait != WAIT_OBJECT_0)
            break;
        size_t size = m_stack.size();
        if (size)
        {
            cmd = m_stack.top();
            m_stack.pop();
        }
        ReleaseMutex(g_hStackMutex);

        if (!size) // データがない？
        {
            if (prev_size != size) // 前とサイズが違う
            {
                // タイマーを再開する
                PostMessageW(g_hMainWnd, WM_COMMAND, ID_RESTART_TIMER, 0);
            }
            else
            {
                // やることないんだから、あんまりCPU時間を食うな。ビジーループを回避
                Sleep(100);
            }
        }
        else
        {
            // タイマーを破棄する
            PostMessageW(g_hMainWnd, WM_COMMAND, ID_KILL_TIMER, 0);

            // 実際に演奏する
            sing_cmd(cmd, false);

            // 設定を保存する
            cmd.save_settings();

            // 演奏が終わるまで待つ
            vsk_sound_wait(-1);
        }

        prev_size = size;
    }

    return 0;
}

DWORD WINAPI ThreadFunc(LPVOID arg)
{
    SERVER *pServer = (SERVER *)arg;
    return pServer->thread_proc();
}

bool SERVER::start()
{
    // サウンドを初期化
    if (!vsk_sound_init(m_stereo))
        return false;

    // 演奏用スレッドを開始
    m_hThread = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
    if (!m_hThread)
        return false;

    return true;
}

BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
    g_hMainWnd = hwnd;

    SERVER *pServer = (SERVER *)lpCreateStruct->lpCreateParams;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pServer);

    SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
    return pServer->start();
}

void OnDestroy(HWND hwnd)
{
    KillTimer(hwnd, TIMER_ID);
    vsk_sound_exit();

    PostQuitMessage(0);
    g_hMainWnd = NULL;
}

BOOL OnCopyData(HWND hwnd, HWND hwndFrom, PCOPYDATASTRUCT pcds)
{
    if (pcds->dwData != 0xDEADFACE)
        return FALSE;

    KillTimer(hwnd, TIMER_ID);
    SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);

    DWORD cbData = pcds->cbData;
    LPWSTR lpData = (LPWSTR)pcds->lpData;
    std::wstring cmd_line = L"cmd_sing_server.exe ";
    cmd_line += lpData;

    SERVER *pServer = Server_GetData(hwnd);

    INT argc;
    LPWSTR *argv = CommandLineToArgvW(cmd_line.c_str(), &argc);
    do {
        // 設定を読み込む
        SERVER_CMD cmd;
        cmd.load_settings();
        // コマンドラインをパースする
        cmd.parse_cmd_line(argc, argv);
        // 演奏しないで設定のみを更新する
        pServer->sing_cmd(cmd, true);
        // 設定を保存する
        cmd.save_settings();

        DWORD wait = WaitForSingleObject(g_hStackMutex, INFINITE);
        if (wait != WAIT_OBJECT_0)
            break;
        pServer->m_stack.push(cmd);
        ReleaseMutex(g_hStackMutex);
    } while (0);
    LocalFree(argv);

    return TRUE;
}

void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    SERVER *pServer = Server_GetData(hwnd);
    switch (id)
    {
    case ID_RESTART_TIMER:
        KillTimer(hwnd, TIMER_ID);
        SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
        break;
    case ID_KILL_TIMER:
        KillTimer(hwnd, TIMER_ID);
        break;
    }
}

void OnTimer(HWND hwnd, UINT id)
{
    if (id == 999)
    {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
        HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
        HANDLE_MSG(hwnd, WM_COPYDATA, OnCopyData);
        HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);
        HANDLE_MSG(hwnd, WM_TIMER, OnTimer);

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

INT
Server_Main(HINSTANCE hInstance, INT argc, LPWSTR *argv, INT nCmdShow)
{
    // サーバーの二重起動を回避する
    if (HWND hwndServer = find_server_window())
    {
        SetForegroundWindow(hwndServer);
        ShowWindow(hwndServer, nCmdShow);
        return 0;
    }

    // 設定を読み込み、コマンドラインをパースする
    SERVER_CMD cmd;
    cmd.load_settings();
    cmd.parse_cmd_line(argc, argv);

    // コマンドを積む
    SERVER server;
    server.m_stack.push(cmd);

    g_hInst = hInstance;
    InitCommonControls();

    WNDCLASSEXW wcx = { sizeof(wcx) };
    wcx.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = WindowProc;
    wcx.hInstance = hInstance;
    wcx.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wcx.lpszMenuName = NULL;
    wcx.lpszClassName = SERVER_CLASSNAME;
    wcx.hIconSm = (HICON)LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!RegisterClassEx(&wcx))
    {
        MessageBoxA(NULL, "RegisterClassEx failed", NULL, MB_ICONERROR);
        return -1;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exstyle = 0;
    RECT rc = { 0, 0, 320, 200 };
    AdjustWindowRectEx(&rc, style, FALSE, exstyle);

    HWND hwnd = CreateWindowExW(exstyle, SERVER_CLASSNAME, SERVER_TITLE, style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, &server);
    if (!hwnd)
    {
        MessageBoxA(NULL, "CreateWindowEx failed", NULL, MB_ICONERROR);
        return -2;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (INT)msg.wParam;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    g_hStackMutex = CreateMutexW(NULL, FALSE, NULL);
    INT argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    INT ret = Server_Main(hInstance, argc, argv, nCmdShow);
    LocalFree(argv);
    CloseHandle(g_hStackMutex);
    return ret;
}
