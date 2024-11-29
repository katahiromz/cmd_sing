#include "types.h"
#include "sound.h"
#include "encoding.h"
#include "ast.h"
#include <cstdio>

#include "freealut/include/AL/alut.h"
#include "fmgon/soundplayer.h"
#include "scanner.h"

//////////////////////////////////////////////////////////////////////////////

#define VSK_MAX_CHANNEL 6

std::shared_ptr<VskSoundPlayer> vsk_sound_player;
VskSoundSetting                 vsk_ssg_sound_settings[VSK_MAX_CHANNEL];

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
};

VskAstPtr vsk_get_sing_param(const VskSingItem& item)
{
    if (item.m_param.empty())
        return nullptr;
    return vsk_eval_text(item.m_param);
} // vsk_get_sing_param

bool vsk_phrase_from_sing_items(
    std::shared_ptr<VskPhrase> phrase,
    const std::vector<VskSingItem>& items)
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
                    phrase->m_setting.m_length = (24.0f * 4.0f) / i0;
                    continue;
                }
            }
            return false;
        case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B':
        case 'R':
            // 音符か休符
            if (auto ast = vsk_get_sing_param(item)) {
                auto L = ast->to_int();
                // NOTE: 24 is the length of a quarter note
                if ((1 <= L) && (L <= 32)) {
                    length = float(24 * 4 / L);
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

bool vsk_expand_sing_items(std::vector<VskSingItem>& items)
{
retry:;
    size_t k = VskString::npos;
    int level = 0, repeat = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_subcommand == "RP") {
            // TODO:
            repeat = 1;
        }
        if (items[i].m_subcommand == "[") {
            k = i;
            ++level;
        }
        if (items[i].m_subcommand == "]") {
            --level;
            if ((level == 0) && (repeat != 0)) {
                std::vector<VskSingItem> sub(items.begin() + k + 1, items.begin() + i);
                std::vector<VskSingItem> children;
                for (int m = 0; m < repeat; ++m) {
                    children.insert(children.end(), sub.begin(), sub.end());
                }
                items.erase(items.begin() + k, items.begin() + i + 1);
                items.insert(items.begin() + k, children.begin(), children.end());
                goto retry;
            }
        }
    }
    return true;
} // vsk_expand_sing_items

bool vsk_eval_sing_items(
    std::vector<VskSingItem>& items, 
    const VskString& expr)
{
    VskScanner scanner(expr);
    items.clear();

    VskSingItem item;
    VskString subcommand;
    while (!scanner.eof()) {
        char ch = vsk_toupper(scanner.getch());
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

    return vsk_expand_sing_items(items);
} // vsk_eval_sing_items

// CMD SING文実装の本体
bool vsk_sound_sing(const VskString& str)
{
    std::vector<VskSingItem> items;
    if (!vsk_eval_sing_items(items, str))
        return false;

    // create phrase
    auto phrase = std::make_shared<VskPhrase>();
    phrase->m_setting = vsk_ssg_sound_settings[0];
    phrase->m_setting.m_fm = false;
    if (!vsk_phrase_from_sing_items(phrase, items))
        return false;

    VskScoreBlock block = { phrase };
    vsk_ssg_sound_settings[0] = phrase->m_setting;
    vsk_sound_player->play(block);
    return true;
}

//////////////////////////////////////////////////////////////////////////////

bool vsk_sound_init(void)
{
    if (!alutInit(NULL, NULL))
    {
        ALenum error = alutGetError();
        std::fprintf(stderr, "%s\n", alutGetErrorString(error));
        return false;
    }
    vsk_sound_player = std::make_shared<VskSoundPlayer>();
    return true;
}

void vsk_sound_stop(void)
{
    if (vsk_sound_player)
        vsk_sound_player->stop();
}

void vsk_sound_exit(void)
{
    vsk_sound_stop();
    alutExit();
    vsk_sound_player = nullptr;
}

bool vsk_sound_is_playing(void)
{
    return vsk_sound_player && vsk_sound_player->m_playing_music;
}

bool vsk_sound_wait(VskDword milliseconds)
{
    if (vsk_sound_is_playing())
        return vsk_sound_player->wait_for_stop(milliseconds);
    return false;
}

void vsk_sound_beep(int i)
{
    assert(vsk_sound_player);
    if (vsk_sound_player) {
        vsk_sound_player->beep(i);
    }
}

bool vsk_sound_is_beeping(void)
{
    return vsk_sound_player && vsk_sound_player->is_beeping();
}
