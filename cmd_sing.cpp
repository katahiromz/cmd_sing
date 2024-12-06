#include "types.h"
#include "sound.h"
#include "encoding.h"
#include "ast.h"
#include <cstdio>
#include <cassert>

#include "freealut/include/AL/alut.h"   // OpenAL utility
#include "fmgon/soundplayer.h"          // �T�E���h�v���[���[
#include "scanner.h"                    // VskScanner

// �T�E���h�v���[���[
extern std::shared_ptr<VskSoundPlayer> vsk_sound_player;
// CMD SING�̌��݂̐ݒ�
static VskSoundSetting vsk_cmd_sing_settings;

// VskSingItem --- CMD SING �p�̉��t����
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

    // �f�o�b�O�p
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

// �f�o�b�O�p
VskString vsk_string_from_sing_items(const std::vector<VskSingItem>& items)
{
    VskString ret;
    for (auto& item : items)
        ret += item.to_str();
    return ret;
}

// CMD SING�̃p�����[�^���擾����
VskAstPtr vsk_get_sing_param(const VskSingItem& item)
{
    if (item.m_param.empty())
        return nullptr;
    return vsk_eval_text(item.m_param);
} // vsk_get_sing_param

// CMD SING�̍��ڌQ����t���[�Y���쐬����
bool vsk_phrase_from_sing_items(std::shared_ptr<VskPhrase> phrase, const std::vector<VskSingItem>& items)
{
    float length;
    for (auto& item : items) {
        char ch = item.m_subcommand[0];
        switch (ch) {
        case 'T': // Tempo (�e���|)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((48 <= i0) && (i0 <= 255)) {
                    phrase->m_setting.m_tempo = i0;
                    continue;
                }
            }
            return false;
        case 'O': // Octave (�I�N�^�[�u)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((3 <= i0) && (i0 <= 6)) {
                    phrase->m_setting.m_octave = i0 - 1;
                    continue;
                }
            }
            return false;
        case 'L': // Length (�����E�x���̒���)
            if (auto ast = vsk_get_sing_param(item)) {
                auto i0 = ast->to_int();
                if ((1 <= i0) && (i0 <= 32)) {
                    phrase->m_setting.m_length = (24.0f * 4) / i0;
                    continue;
                }
            }
            return false;
        case 'R':
            if (item.m_subcommand == "RP") { // �J��Ԃ��H
                break;
            }
            // ...FALL THROUGH...
        case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B':
            // ����(CDEFGAB)���x��(Rest)
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
            // �X�y�V�����A�N�V����
            if (auto ast = vsk_get_sing_param(item)) {
                int action_no = ast->to_int();
                phrase->add_action_node(ch, action_no);
                continue;
            }
            return false;
        }
    }
    return true;
} // vsk_phrase_from_sing_items

// CMD SING�̍��ڂ̌J��Ԃ�(RP)��W�J����B
bool vsk_expand_sing_items_repeat(std::vector<VskSingItem>& items)
{
retry:;
    size_t k = VskString::npos, n = VskString::npos;
    int level = 0, repeat = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_subcommand == "RP") { // �J��Ԃ��irepeat�j
            auto ast = vsk_get_sing_param(items[i]);
            repeat = ast->to_int();
            if (repeat < 0) { // �}�C�i�X�̌J��Ԃ��͂�������
                assert(0);
                return false;
            }
            n = i;
            continue;
        }
        if (items[i].m_subcommand == "[") { // �J��Ԃ��̃J�b�R�͂���
            if (n == VskString::npos) {
                assert(0);
                return false;
            }
            k = i;
            ++level;
            continue;
        }
        if (items[i].m_subcommand == "]") { // �J��Ԃ��̃J�b�R�I���
            --level;
            std::vector<VskSingItem> sub(items.begin() + k + 1, items.begin() + i);
            items.erase(items.begin() + n, items.begin() + i + 1);
			for (int m = 0; m < repeat; ++m) {
				auto insert_position = k - 1 <= items.size() ?
					items.begin() + (k - 1) : items.end();
				items.insert(insert_position, sub.begin(), sub.end());
			}
            goto retry; // �P�W�J������ŏ������蒼��
        }
    }
    return true;
} // vsk_expand_sing_items_repeat

// �����񂩂�CMD SING�̍��ڂ��擾����
bool vsk_sing_items_from_string(std::vector<VskSingItem>& items, const VskString& expr)
{
    // �啶���ɂ���
    auto str = expr;
    vsk_upper(str);

    // �X�L���i�[���g���Ď����͂��n�߂�
    VskScanner scanner(str);
    items.clear();
    VskSingItem item;
    VskString subcommand;
    while (!scanner.eof()) { // ������̏I���܂�
        char ch = scanner.getch(); // �ꕶ���擾
        if (vsk_isblank(ch)) continue; // �󔒂͖���
        if (ch == ';') continue;

        if (ch == '[') { // �J��Ԃ��̃J�b�R�͂��߁H
            item.m_subcommand = "[";
            items.push_back(item);
            item.clear();
            continue;
        }
        if (ch == ']') { // �J��Ԃ��̃J�b�R�I���H
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
                    status = 1; // �������P���邩������Ȃ�
                    break;
                case 'R':
                    status = 1; // �������P���邩������Ȃ�
                    if (scanner.peek() == 'P') { // "RP" (repeat)�H
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
            return false; // �������Ȃ��̂ɂ����ɗ���̂͂�������
        }
        item.m_subcommand = subcommand;
        subcommand.clear();

        // �V���[�v���t���b�g
        ch = scanner.peek();
        if ((ch == '+') || (ch == '#') || (ch == '-')) {
            item.m_sign = scanner.getch();
            ch = scanner.peek();
        }

        // �p�����[�^���擾����
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

        // �t�_�i�h�b�g�j
        ch = scanner.peek();
        if (ch == '.') {
            item.m_dot = true;
            scanner.getch();
        }

        // ���ڂ�ǉ�
        items.push_back(item);
        item.clear();
    }

    // �J��Ԃ���W�J����
    return vsk_expand_sing_items_repeat(items);
} // vsk_sing_items_from_string

// CMD SING�������̖{��
bool vsk_sound_cmd_sing(const VskString& str)
{
    // �����񂩂�CMD SING�̍��ڂ��擾����
    std::vector<VskSingItem> items;
    if (!vsk_sing_items_from_string(items, str))
        return false; // ���s

    // �t���[�Y���쐬����
    auto phrase = std::make_shared<VskPhrase>();
    phrase->m_setting = vsk_cmd_sing_settings;
    phrase->m_setting.m_fm = false;
    if (!vsk_phrase_from_sing_items(phrase, items))
        return false;

    // �t���[�Y�����t����
    VskScoreBlock block = { phrase };
    vsk_sound_player->play(block);

    // �ݒ��ۑ�����
    vsk_cmd_sing_settings = phrase->m_setting;

    return true; // ����
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
