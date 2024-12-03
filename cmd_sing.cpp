#include "types.h"
#include "sound.h"
#include "encoding.h"
#include "ast.h"
#include <cstdio>
#include <cassert>
#include <iostream>

#include "freealut/include/AL/alut.h"
#include "fmgon/soundplayer.h"
#include "scanner.h"

extern std::shared_ptr<VskSoundPlayer> vsk_sound_player;
static VskSoundSetting                 vsk_cmd_sing_settings;

//////////////////////////////////////////////////////////////////////////////

// VskSingItem --- CMD SING 用の演奏項目
struct VskSingItem
{
    VskString               m_subcommand;
    VskString               m_param;
    char                    m_sign;
    bool                    m_dot;

    VskSingItem() { clear(); }

    void clear() {
        m_subcommand.clear();
        m_param.clear();
        m_sign = 0;
        m_dot = false;
    }

    VskString to_str() const {
        VskString ret = m_subcommand;
        if (m_sign)
            ret += m_sign;
        ret += m_param;
        if (m_dot)
            ret += '.';
        return ret;
    }
};

// デバッグ用
VskString vsk_string_from_sing_items(const std::vector<VskSingItem>& items)
{
    VskString ret;
    for (auto& item : items)
    {
        ret += item.to_str();
    }
    return ret;
}

// CMD SINGのパラメータを取得する
VskAstPtr vsk_get_sing_param(const VskSingItem& item)
{
    if (item.m_param.empty())
        return nullptr;
    return vsk_eval_text(item.m_param);
} // vsk_get_sing_param

// CMD SINGの項目群からフレーズを作成する
bool vsk_phrase_from_sing_items(std::shared_ptr<VskPhrase> phrase, const std::vector<VskSingItem>& items)
{
    float length;
    for (auto& item : items) {
        char ch = item.m_subcommand[0];
        switch (ch) {
        case 'T': // Tempo (テンポ)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((48 <= i0) && (i0 <= 255)) {
                    phrase->m_setting.m_tempo = i0;
                    continue;
                }
            }
            return false;
        case 'O': // Octave (オクターブ)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((3 <= i0) && (i0 <= 6)) {
                    phrase->m_setting.m_octave = i0 - 1;
                    continue;
                }
            }
            return false;
        case 'L': // Length (音符・休符の長さ)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((1 <= i0) && (i0 <= 32)) {
                    phrase->m_setting.m_length = (24.0f * 4) / i0;
                    continue;
                }
            }
            return false;
        case 'R':
            if (item.m_subcommand == "RP") { // 繰り返し？
                break;
            }
            // ...FALL THROUGH...
        case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B':
            // 音符(CDEFGAB)か休符(Rest)
            if (auto ast = vsk_get_sing_param(item)) {
                auto L = ast->to_int();
                // NOTE: 24 is the length of a quarter note
                if ((1 <= L) && (L <= 32)) {
                    length = 24.0f * 4 / L;
                } else {
                    return false;
                }
            } else {
                length = phrase->m_setting.m_length;
            }
            phrase->add_note(ch, item.m_dot, length, item.m_sign);
            continue;
        case 'X':
            // 画面表示
            break;
        }
    }
    return true;
} // vsk_phrase_from_sing_items

// CMD SINGの項目の繰り返しを展開する。
bool vsk_expand_sing_items_repeat(std::vector<VskSingItem>& items)
{
retry:;
    size_t k = VskString::npos, n = VskString::npos;
    int level = 0, repeat = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_subcommand == "RP") {
            auto ast = vsk_get_sing_param(items[i]);
            repeat = ast->to_int();
            if (repeat < 0) {
                assert(0);
                return false;
            }
            n = i;
            continue;
        }
        if (items[i].m_subcommand == "[") {
            if (n == VskString::npos) {
                assert(0);
                return false;
            }
            k = i;
            ++level;
            continue;
        }
        if (items[i].m_subcommand == "]") {
            --level;
            std::vector<VskSingItem> sub(items.begin() + k + 1, items.begin() + i);
            items.erase(items.begin() + n, items.begin() + i + 1);
            for (int m = 0; m < repeat; ++m) {
                items.insert(items.begin() + k - 1, sub.begin(), sub.end());
            }
            goto retry;
        }
    }
    return true;
} // vsk_expand_sing_items_repeat

// CMD SINGの項目を評価する
bool vsk_eval_sing_items(std::vector<VskSingItem>& items, const VskString& expr)
{
    // 大文字にする
    auto str = expr;
    vsk_upper(str);

    // スキャナーを使って字句解析を始める
    VskScanner scanner(str);
    items.clear();

    VskSingItem item;
    VskString subcommand;
    while (!scanner.eof()) {
        char ch = scanner.getch();
        if (vsk_isblank(ch)) {
            continue;
        }
        if (ch == ';') {
            continue;
        }
        if (ch == '[') {
            item.m_subcommand = "[";
            items.push_back(item);
            item.clear();
            continue;
        }
        if (ch == ']') {
            item.m_subcommand = "]";
            items.push_back(item);
            item.clear();
            continue;
        }
        int status = 0;
        if (vsk_isupper(ch)) {
            subcommand.push_back(ch);
            if (subcommand.size() == 1) {
                switch (subcommand[0]) {
                case 'T': case 'O': case 'L': 
                case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B':
                case 'X':
                    status = 1;
                    break;
                case 'R':
                    status = 1;
                    if (scanner.peek() == 'P') {
                        subcommand += scanner.getch();
                    }
                    break;
                default:
                    break;
                }
            } else {
                continue;
            }
        }
        if (status == 0) {
            // Illegal function call
            return false;
        }
        item.m_subcommand = subcommand;
        subcommand.clear();

        ch = scanner.peek();
        if ((ch == '+') || (ch == '#') || (ch == '-')) {
            item.m_sign = scanner.getch();
            ch = scanner.peek();
        }

        if (scanner.peek() == '(') {
            int level = 0;
            for (;;) {
                ch = scanner.peek();
                if (ch == 0)
                    break;
                if (ch == '(') {
                    ++level;
                    item.m_param.push_back(ch);
                    scanner.getch();
                    continue;
                }
                if (ch == ')') {
                    item.m_param.push_back(ch);
                    scanner.getch();
                    if (--level == 0)
                        break;
                    continue;
                }
                scanner.getch();
                if (!vsk_isblank(ch)) {
                    item.m_param.push_back(ch);
                }
            }
        } else {
            if (vsk_isdigit(ch)) {
                while (!scanner.eof()) {
                    ch = scanner.getch();
                    if (!vsk_isdigit(ch)) {
                        break;
                    }
                    item.m_param.push_back(ch);
                }
                if (!scanner.eof()) {
                    scanner.ungetch();
                }
            }
        }

        ch = scanner.peek();
        if (ch == '.') {
            item.m_dot = true;
            scanner.getch();
        }

        items.push_back(item);
        item.clear();
    }

    return vsk_expand_sing_items_repeat(items);
} // vsk_eval_sing_items

// CMD SING文実装の本体
bool vsk_sound_cmd_sing(const VskString& str)
{
    std::vector<VskSingItem> items;
    if (!vsk_eval_sing_items(items, str))
        return false;

    // create phrase
    auto phrase = std::make_shared<VskPhrase>();
    phrase->m_setting = vsk_cmd_sing_settings;
    phrase->m_setting.m_fm = false;
    if (!vsk_phrase_from_sing_items(phrase, items))
        return false;

    VskScoreBlock block = { phrase };
    vsk_cmd_sing_settings = phrase->m_setting;
    vsk_sound_player->play(block);
    return true;
}

#ifdef CMD_SING_EXE
int main(int argc, char **argv)
{
    VskString str = ((argc > 1) ? argv[1] : "T255CDEFEDC");

    if (!vsk_sound_init())
    {
        std::fprintf(stderr, "vsk_sound_init failed\n");
        return 1;
    }

    vsk_sound_cmd_sing(str);
    vsk_sound_wait(-1);
    vsk_sound_exit();

    return 0;
}
#endif // def CMD_SING_EXE
