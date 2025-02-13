// Detect memory leaks (for Debug and MSVC)
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

bool g_canceled = false;

inline WORD get_lang_id(void)
{
    return PRIMARYLANGID(LANGIDFROMLCID(GetThreadLocale()));
}

bool g_no_beep = false;

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
        case IDT_VERSION: return TEXT("cmd_sing バージョン 1.3 by 片山博文MZ\n");
        case IDT_HELP:
            return TEXT("使い方: cmd_sing [オプション] 文字列\n")
                   TEXT("\n")
                   TEXT("オプション:\n")
                   TEXT("  -D変数名=値            変数に代入。\n")
                   TEXT("  -save_wav 出力.wav     WAVファイルとして保存。\n")
                   TEXT("  -reset                 設定をリセット。\n")
                   TEXT("  -help                  このメッセージを表示する。\n")
                   TEXT("  -version               バージョン情報を表示する。\n")
                   TEXT("\n")
                   TEXT("文字列変数は、{変数名} で展開できます。\n");
        case IDT_NEEDS_OPERAND: return TEXT("エラー: オプション -save_wav は引数が必要です。\n");
        case IDT_INVALID_OPTION: return TEXT("エラー: 「%s」は、無効なオプションです。\n");
        case IDT_TOO_MAY_ARGS: return TEXT("エラー: 引数が多すぎます。\n");
        case IDT_SOUND_INIT_FAILED: return TEXT("エラー: vsk_sound_initが失敗しました。\n");
        case IDT_CANT_OPEN_FILE: return TEXT("エラー: ファイル「%s」が開けません。\n");
        case IDT_BAD_CALL: return TEXT("エラー: Illegal function call\n");
        case IDT_CIRCULAR_REFERENCE: return TEXT("エラー: 変数の循環参照を検出しました。\n");
        }
    }
    else // The others are Let's la English
#endif
    {
        switch (id)
        {
        case IDT_VERSION: return TEXT("cmd_sing version 1.3 by katahiromz\n");
        case IDT_HELP:
            return TEXT("Usage: cmd_sing [Options] string\n")
                   TEXT("\n")
                   TEXT("Options:\n")
                   TEXT("  -DVAR=VALUE            Assign to a variable.\n")
                   TEXT("  -save_wav output.wav   Save as WAV file.\n")
                   TEXT("  -reset                 Reset settings.\n")
                   TEXT("  -help                  Display this message.\n")
                   TEXT("  -version               Display version info.\n")
                   TEXT("\n")
                   TEXT("String variables can be expanded with {variable name}.\n");
        case IDT_NEEDS_OPERAND: return TEXT("ERROR: Option -save_wav needs an operand.\n");
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

// 変数
std::map<VskString, VskString> g_variables;

// 再帰的に「{変数名}」を変数の値に置き換える関数
std::string
vsk_replace_placeholders(const std::string& str, std::unordered_set<std::string>& visited) {
    std::string result = str;
    size_t start_pos = 0;

    while ((start_pos = result.find("{", start_pos)) != result.npos) {
        size_t end_pos = result.find("}", start_pos);
        if (end_pos == std::string::npos)
            break; // 閉じカッコが見つからない場合は終了

        std::string key = result.substr(start_pos + 1, end_pos - start_pos - 1);
        if (visited.find(key) != visited.end()) {
            // 循環参照を検出した場合はエラーとして処理する
            throw std::runtime_error("circular reference detected");
        }
        visited.insert(key);

        auto it = g_variables.find(key);
        if (it != g_variables.end()) {
            // ここで再帰的に置き換えを行う
            std::string value = vsk_replace_placeholders(it->second, visited);
            result.replace(start_pos, end_pos - start_pos + 1, value);
            start_pos += value.length(); // 置き換えた後の新しい開始位置に移動
        } else {
            result.replace(start_pos, end_pos - start_pos + 1, "");
        }
        visited.erase(key);
    }

    return result;
}

// 再帰的に「{変数名}」を変数の値に置き換える関数
std::string vsk_replace_placeholders(const std::string& str)
{
    std::unordered_set<std::string> visited;
    return vsk_replace_placeholders(str, visited);
}

// 再帰的に「[変数名]」を変数の値に置き換える関数
std::string
vsk_replace_placeholders2(const std::string& str, std::unordered_set<std::string>& visited) {
    std::string result = str;
    size_t start_pos = 0;

    while ((start_pos = result.find("[", start_pos)) != result.npos) {
        size_t end_pos = result.find("]", start_pos);
        if (end_pos == std::string::npos)
            break; // 閉じカッコが見つからない場合は終了

        std::string key = result.substr(start_pos + 1, end_pos - start_pos - 1);
        CharUpperA(&key[0]);
        if (visited.find(key) != visited.end()) {
            // 循環参照を検出した場合はエラーとして処理する
            throw std::runtime_error("circular reference detected");
        }
        visited.insert(key);

        auto it = g_variables.find(key);
        if (it != g_variables.end()) {
            // ここで再帰的に置き換えを行う
            std::string value = vsk_replace_placeholders2(it->second, visited);
            result.replace(start_pos, end_pos - start_pos + 1, value);
            start_pos += value.length(); // 置き換えた後の新しい開始位置に移動
        } else {
            result.replace(start_pos, end_pos - start_pos + 1, "");
        }
        visited.erase(key);
    }

    return result;
}

// 再帰的に「[変数名]」を変数の値に置き換える関数
std::string vsk_replace_placeholders2(const std::string& str)
{
    std::unordered_set<std::string> visited;
    return vsk_replace_placeholders2(str, visited);
}

// ワイド文字列をSJIS文字列に変換
std::string vsk_sjis_from_wide(const wchar_t *wide)
{
    int size = WideCharToMultiByte(932, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0)
        return "";
    std::string str;
    str.resize(size - 1);
    WideCharToMultiByte(932, 0, wide, -1, &str[0], size, nullptr, nullptr);
    return str;
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
    bool m_reset = false;
    bool m_stereo = false;

    RET parse_cmd_line(int argc, wchar_t **argv);
    RET run();
    bool load_settings();
    bool save_settings();
};

bool CMD_SING::load_settings()
{
    HKEY hKey;

    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                  KEY_READ, &hKey);
    if (error)
        return false;

    size_t size = vsk_cmd_sing_get_setting_size();
    std::vector<uint8_t> setting;
    setting.resize(size);

    DWORD cbValue = size;
    error = RegQueryValueExW(hKey, L"setting", NULL, NULL, setting.data(), &cbValue);
    if (!error)
        vsk_cmd_sing_set_setting(setting);

    RegCloseKey(hKey);

    return true;
}

bool CMD_SING::save_settings()
{
    std::vector<uint8_t> setting;
    if (!vsk_cmd_sing_get_setting(setting))
        return false;

    HKEY hKey;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Katayama Hirofumi MZ\\cmd_sing", 0,
                                    NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    if (error)
        return false;

    RegSetValueExW(hKey, L"setting", 0, REG_BINARY, setting.data(), (DWORD)setting.size());

    RegCloseKey(hKey);

    return true;
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

        if (_wcsicmp(arg, L"-save_wav") == 0 || _wcsicmp(arg, L"--save_wav") == 0)
        {
            if (iarg + 1 < argc)
            {
                m_output_file = argv[++iarg];
                continue;
            }
            else
            {
                my_puts(get_text(IDT_NEEDS_OPERAND), stderr);
                return RET_BAD_CMDLINE;
            }
        }

        if (_wcsicmp(arg, L"-reset") == 0 || _wcsicmp(arg, L"--reset") == 0)
        {
            m_reset = true;
            continue;
        }

        if (_wcsicmp(arg, L"-stereo") == 0 || _wcsicmp(arg, L"--stereo") == 0)
        {
            m_stereo = true;
            continue;
        }

        if (_wcsicmp(arg, L"-no-beep") == 0 || _wcsicmp(arg, L"--no-beep") == 0)
        {
            g_no_beep = true;
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
            g_variables[var] = value;
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

    if (!vsk_sound_init(m_stereo))
    {
        my_puts(get_text(IDT_SOUND_INIT_FAILED), stderr);
        return RET_BAD_SOUND_INIT;
    }

    if (!m_reset)
        load_settings();

    if (m_output_file.size())
    {
        if (VSK_SOUND_ERR ret = vsk_sound_cmd_sing_save(m_str_to_play.c_str(), m_output_file.c_str(), m_stereo))
        {
            RET ret2 = RET_BAD_CALL;
            switch (ret)
            {
            case 1:
                my_puts(get_text(IDT_BAD_CALL), stderr);
                break;
            case 2:
                my_printf(stderr, get_text(IDT_CANT_OPEN_FILE), m_output_file.c_str());
                ret2 = RET_CANT_OPEN_FILE;
                break;
            default:
                assert(0);
            }
            do_beep();
            vsk_sound_exit();
            return ret2;
        }
        vsk_sound_exit();
        return RET_SUCCESS;
    }

    if (VSK_SOUND_ERR ret = vsk_sound_cmd_sing(m_str_to_play.c_str(), m_stereo))
    {
        RET ret2 = RET_BAD_CALL;
        switch (ret)
        {
        case 1:
            my_puts(get_text(IDT_BAD_CALL), stderr);
            break;
        case 2:
            my_puts(get_text(IDT_CANT_OPEN_FILE), stderr);
            ret2 = RET_CANT_OPEN_FILE;
            break;
        default:
            assert(0);
        }
        do_beep();
        vsk_sound_exit();
        return ret2;
    }

    vsk_sound_wait(-1);

    save_settings();

    vsk_sound_exit();

    return RET_SUCCESS;
}

static BOOL WINAPI HandlerRoutine(DWORD signal)
{
    switch (signal)
    {
    case CTRL_C_EVENT: // Ctrl+C
    case CTRL_BREAK_EVENT: // Ctrl+Break
        g_canceled = true;
        vsk_sound_stop();
        std::printf("^C\nBreak\nOk\n");
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
        std::fflush(stdout);
        do_beep();
        return RET_CANCELED;
    }

    return ret;
}
