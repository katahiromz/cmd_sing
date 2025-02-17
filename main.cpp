﻿// Detect memory leaks (for Debug and MSVC)
#if defined(_MSC_VER) && !defined(NDEBUG) && !defined(_CRTDBG_MAP_ALLOC)
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif

#include "sound.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <windows.h>
#include <tchar.h>
#include <shlwapi.h>
#include <strsafe.h>

enum RET { // exit code of this program
    RET_SUCCESS = 0,
    RET_BAD_CALL = 2,
    RET_BAD_CMDLINE = 3,
    RET_CANT_OPEN_FILE = 4,
    RET_BAD_SOUND_INIT = 5,
    RET_CANCELED = 6,
};

inline WORD get_lang_id(void)
{
    return PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
}

static bool g_no_beep = false;

void do_beep(void)
{
#ifdef ENABLE_BEEP
    if (!g_no_beep)
        Beep(2400, 500);
#endif
}

void my_puts(LPCTSTR pszText, FILE *fout)
{
#ifdef UNICODE
    _ftprintf(fout, _T("%ls"), pszText);
#else
    fputs(pszText, fout);
#endif
}

void my_printf(FILE *fout, LPCTSTR  fmt, ...)
{
    static TCHAR szText[2048];
    va_list va;

    va_start(va, fmt);
    StringCchVPrintf(szText, _countof(szText), fmt, va);
    my_puts(szText, fout);
    va_end(va);
}

enum TEXT_ID { // text id
    IDT_VERSION,
    IDT_HELP,
    IDT_NEEDS_OPERAND,
    IDT_INVALID_OPTION,
    IDT_TOO_MAY_ARGS,
    IDT_SOUND_INIT_FAILED,
    IDT_CANT_OPEN_FILE,
    IDT_BAD_CALL,
    IDT_CIRCULAR_REFERENCE,
};

// localization
LPCTSTR get_text(INT id)
{
#ifdef JAPAN
    if (get_lang_id() == LANG_JAPANESE) // Japone for Japone
    {
        switch (id)
        {
        case IDT_VERSION: return TEXT("cmd_sing バージョン 1.7 by 片山博文MZ\n");
        case IDT_HELP:
            return TEXT("使い方: cmd_sing [オプション] 文字列\n")
                   TEXT("\n")
                   TEXT("オプション:\n")
                   TEXT("  -D変数名=値            変数に代入。\n")
                   TEXT("  -save-wav 出力.wav     WAVファイルとして保存。\n")
                   TEXT("  -stopm                 音楽を止めて設定をリセット。\n")
                   TEXT("  -stereo                音をステレオにする（デフォルト）。\n")
                   TEXT("  -mono                  音をモノラルにする。\n")
                   TEXT("  -help                  このメッセージを表示する。\n")
                   TEXT("  -version               バージョン情報を表示する。\n")
                   TEXT("\n")
                   TEXT("文字列変数は、{変数名} で展開できます。\n");
        case IDT_NEEDS_OPERAND: return TEXT("エラー: オプション「%s」は引数が必要です。\n");
        case IDT_INVALID_OPTION: return TEXT("エラー: 「%s」は、無効なオプションです。\n");
        case IDT_TOO_MAY_ARGS: return TEXT("エラー: 引数が多すぎます。\n");
        case IDT_SOUND_INIT_FAILED: return TEXT("エラー: vsk_sound_initが失敗しました。\n");
        case IDT_CANT_OPEN_FILE: return TEXT("エラー: ファイル「%s」が開けません。\n");
        case IDT_BAD_CALL: return TEXT("エラー: 不正な関数呼び出しです。\n");
        case IDT_CIRCULAR_REFERENCE: return TEXT("エラー: 変数の循環参照を検出しました。\n");
        }
    }
    else // The others are Let's la English
#endif
    {
        switch (id)
        {
        case IDT_VERSION: return TEXT("cmd_sing version 1.7 by katahiromz\n");
        case IDT_HELP:
            return TEXT("Usage: cmd_sing [Options] string\n")
                   TEXT("\n")
                   TEXT("Options:\n")
                   TEXT("  -DVAR=VALUE            Assign to a variable.\n")
                   TEXT("  -save-wav output.wav   Save as WAV file.\n")
                   TEXT("  -stopm                 Stop music and reset settings.\n")
                   TEXT("  -stereo                Make sound stereo (default).\n")
                   TEXT("  -mono                  Make sound mono.\n")
                   TEXT("  -help                  Display this message.\n")
                   TEXT("  -version               Display version info.\n")
                   TEXT("\n")
                   TEXT("String variables can be expanded with {variable name}.\n");
        case IDT_NEEDS_OPERAND: return TEXT("ERROR: Option '%s' needs an operand.\n");
        case IDT_INVALID_OPTION: return TEXT("ERROR: '%s' is an invalid option.\n");
        case IDT_TOO_MAY_ARGS: return TEXT("ERROR: Too many arguments.\n");
        case IDT_SOUND_INIT_FAILED: return TEXT("ERROR: vsk_sound_init failed.\n");
        case IDT_CANT_OPEN_FILE: return TEXT("ERROR: Unable to open file '%s'.\n");
        case IDT_BAD_CALL: return TEXT("ERROR: Illegal function call\n");
        case IDT_CIRCULAR_REFERENCE: return TEXT("ERROR: Circular variable reference detected.\n");
        }
    }

    assert(0);
    return nullptr;
}

void version(void)
{
    my_puts(get_text(IDT_VERSION), stdout);
}

void usage(void)
{
    my_puts(get_text(IDT_HELP), stdout);
}

VSK_SOUND_ERR vsk_sound_cmd_sing(const wchar_t *wstr, bool stereo)
{
    return vsk_sound_cmd_sing(vsk_sjis_from_wide(wstr).c_str(), stereo);
}

VSK_SOUND_ERR vsk_sound_cmd_sing_save(const wchar_t *wstr, const wchar_t *filename, bool stereo)
{
    return vsk_sound_cmd_sing_save(vsk_sjis_from_wide(wstr).c_str(), filename, stereo);
}

struct CMD_SING
{
    bool m_help = false;
    bool m_version = false;
    std::wstring m_str_to_play;
    std::wstring m_output_file;
    bool m_stopm = false;
    bool m_stereo = true;
    bool m_no_reg = false;
    std::map<VskString, VskString> m_variables;

    RET parse_cmd_line(int argc, wchar_t **argv);
    RET run();
    bool load_settings();
    bool save_settings();
};

// レジストリから設定を読み込む
bool CMD_SING::load_settings()
{
    if (m_no_reg)
        return false;

    HKEY hKey;

    // レジストリを開く
    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                  KEY_READ, &hKey);
    if (error)
        return false;

    // ステレオかモノラルか？
    {
        DWORD dwValue, cbValue = sizeof(dwValue);
        error = RegQueryValueExW(hKey, L"Stereo", NULL, NULL, (BYTE*)&dwValue, &cbValue);
        if (!error)
            m_stereo = !!dwValue;
    }

    // 音声の設定のサイズ
    size_t size = vsk_cmd_sing_get_setting_size();
    std::vector<uint8_t> setting;
    setting.resize(size);

    // 設定項目を読み込む
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
bool CMD_SING::save_settings()
{
    if (m_no_reg)
        return false;

    std::vector<uint8_t> setting;
    if (!vsk_cmd_sing_get_setting(setting))
        return false;

    // レジストリを作成
    HKEY hKey;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                    NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL);
    if (error)
        return false;

    // ステレオかモノラルか？
    {
        DWORD dwValue = !!m_stereo, cbValue = sizeof(dwValue);
        RegSetValueExW(hKey, L"Stereo", 0, REG_DWORD, (BYTE *)&dwValue, cbValue);
    }

    // 音声の設定を書き込む
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

// アプリのレジストリキーを消す
void erase_reg_settings(void)
{
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing");
}

RET CMD_SING::parse_cmd_line(int argc, wchar_t **argv)
{
    if (argc <= 1)
    {
        m_help = true;
        return RET_SUCCESS;
    }

    for (int iarg = 1; iarg < argc; ++iarg)
    {
        LPWSTR arg = argv[iarg];

        if (_wcsicmp(arg, L"-help") == 0 || _wcsicmp(arg, L"--help") == 0)
        {
            m_help = true;
            return RET_SUCCESS;
        }

        if (_wcsicmp(arg, L"-version") == 0 || _wcsicmp(arg, L"--version") == 0)
        {
            m_version = true;
            return RET_SUCCESS;
        }

        if (_wcsicmp(arg, L"-save-wav") == 0 || _wcsicmp(arg, L"--save-wav") == 0 ||
            _wcsicmp(arg, L"-save_wav") == 0 || _wcsicmp(arg, L"--save_wav") == 0)
        {
            if (iarg + 1 < argc)
            {
                m_output_file = argv[++iarg];
                continue;
            }
            else
            {
                my_printf(stderr, get_text(IDT_NEEDS_OPERAND), arg);
                return RET_BAD_CMDLINE;
            }
        }

        if (_wcsicmp(arg, L"-stopm") == 0 || _wcsicmp(arg, L"--stopm") == 0)
        {
            m_stopm = true;
            continue;
        }

        if (_wcsicmp(arg, L"-stereo") == 0 || _wcsicmp(arg, L"--stereo") == 0)
        {
            m_stereo = true;
            continue;
        }

        if (_wcsicmp(arg, L"-mono") == 0 || _wcsicmp(arg, L"--mono") == 0)
        {
            m_stereo = false;
            continue;
        }

        // hidden feature
        if (_wcsicmp(arg, L"-no-beep") == 0 || _wcsicmp(arg, L"--no-beep") == 0)
        {
            g_no_beep = true;
            continue;
        }

        // hidden feature
        if (_wcsicmp(arg, L"-no-reg") == 0 || _wcsicmp(arg, L"--no-reg") == 0)
        {
            m_no_reg = true;
            continue;
        }

        if (arg[0] == '-' && (arg[1] == 'd' || arg[1] == 'D'))
        {
            VskString str = vsk_sjis_from_wide(&arg[2]);
            auto ich = str.find('=');
            if (ich == str.npos)
            {
                my_printf(stderr, get_text(IDT_INVALID_OPTION), arg);
                return RET_BAD_CMDLINE;
            }
            auto var = str.substr(0, ich);
            auto value = str.substr(ich + 1);
            CharUpperA(&var[0]);
            CharUpperA(&value[0]);
            m_variables[var] = value;
            continue;
        }

        if (arg[0] == '-')
        {
            my_printf(stderr, get_text(IDT_INVALID_OPTION), arg);
            return RET_BAD_CMDLINE;
        }

        if (m_str_to_play.empty())
        {
            m_str_to_play = arg;
            continue;
        }

        my_puts(get_text(IDT_TOO_MAY_ARGS), stderr);
        return RET_BAD_CMDLINE;
    }

    return RET_SUCCESS;
}

RET CMD_SING::run()
{
    if (m_help)
    {
        usage();
        return RET_SUCCESS;
    }

    if (m_version)
    {
        version();
        return RET_SUCCESS;
    }

    load_settings();

    if (!vsk_sound_init(m_stereo))
    {
        my_puts(get_text(IDT_SOUND_INIT_FAILED), stderr);
        return RET_BAD_SOUND_INIT;
    }

    if (m_stopm) // 音楽を止めて設定をリセットする
    {
        // TODO: 音楽を止める
        g_variables.clear();
        vsk_cmd_sing_reset_settings();
    }

    // g_variablesをm_variablesで上書き
    for (auto& pair : m_variables)
        g_variables[pair.first] = pair.second;

    if (m_output_file.size())
    {
        if (VSK_SOUND_ERR err = vsk_sound_cmd_sing_save(m_str_to_play.c_str(), m_output_file.c_str(), m_stereo))
        {
            RET ret = RET_BAD_CALL;
            switch (err)
            {
            case VSK_SOUND_ERR_ILLEGAL:
                my_puts(get_text(IDT_BAD_CALL), stderr);
                break;
            case VSK_SOUND_ERR_IO_ERROR:
                my_printf(stderr, get_text(IDT_CANT_OPEN_FILE), m_output_file.c_str());
                ret = RET_CANT_OPEN_FILE;
                break;
            default:
                assert(0);
            }
            do_beep();
            save_settings();
            vsk_sound_exit();
            return ret;
        }
        vsk_sound_exit();
        return RET_SUCCESS;
    }

    if (VSK_SOUND_ERR err = vsk_sound_cmd_sing(m_str_to_play.c_str(), m_stereo))
    {
        RET ret = RET_BAD_CALL;
        switch (err)
        {
        case VSK_SOUND_ERR_ILLEGAL:
            my_puts(get_text(IDT_BAD_CALL), stderr);
            break;
        case VSK_SOUND_ERR_IO_ERROR:
            my_puts(get_text(IDT_CANT_OPEN_FILE), stderr);
            ret = RET_CANT_OPEN_FILE;
            break;
        default:
            assert(0);
        }
        do_beep();
        save_settings();
        vsk_sound_exit();
        return ret;
    }

    vsk_sound_wait(-1);

    save_settings();
    vsk_sound_exit();

    return RET_SUCCESS;
}

#ifdef CMD_SING_EXE
static bool g_canceled = false; // Ctrl+Cなどが押されたか？

static BOOL WINAPI HandlerRoutine(DWORD signal)
{
    switch (signal)
    {
    case CTRL_C_EVENT: // Ctrl+C
    case CTRL_BREAK_EVENT: // Ctrl+Break
        g_canceled = true;
        vsk_sound_stop();
        //std::printf("^C\nBreak\nOk\n"); // このハンドラで時間を掛けちゃダメだ。
        //std::fflush(stdout); // このハンドラで時間を掛けちゃダメだ。
        //do_beep(); // このハンドラで時間を掛けちゃダメだ。
        return TRUE;
    }
    return FALSE;
}

int wmain(int argc, wchar_t **argv)
{
    SetConsoleCtrlHandler(HandlerRoutine, TRUE); // Ctrl+C

    CMD_SING sing;
    if (RET ret = sing.parse_cmd_line(argc, argv))
    {
        do_beep();
        return ret;
    }

    return sing.run();
}

#include <clocale>

int main(void)
{
    // Unicode console output support
    std::setlocale(LC_ALL, "");

    int ret;
    try
    {
        int argc;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        ret = wmain(argc, argv);
        LocalFree(argv);
    } catch (const std::exception& e) {
        std::string what = e.what();
        if (what == "circular reference detected") {
            my_puts(get_text(IDT_CIRCULAR_REFERENCE), stderr);
            do_beep();
        } else {
            printf("ERROR: %s\n", what.c_str());
            do_beep();
        }
        ret = RET_BAD_CALL;
    }

    // Detect handle leaks (for Debug)
#if (_WIN32_WINNT >= 0x0500) && !defined(NDEBUG)
    TCHAR szText[MAX_PATH];
    wnsprintf(szText, _countof(szText), TEXT("GDI Objects: %ld, User Objects: %ld\n"),
              GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
              GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
    OutputDebugString(szText);
#endif

    // Detect memory leaks (for Debug and MSVC)
#if defined(_MSC_VER) && !defined(NDEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    if (g_canceled)
    {
        std::printf("^C\nBreak\nOk\n");
        std::fflush(stdout);
        do_beep();
        erase_reg_settings();
        return RET_CANCELED;
    }

    std::printf("Ok\n");
    return ret;
}
#endif  // def CMD_SING_EXE
