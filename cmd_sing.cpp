#include "sound.h"

int main(int argc, char **argv)
{
    VskString str = ((argc > 1) ? argv[1] : "T255CDEFEDC");

    if (!vsk_sound_init())
    {
        std::fprintf(stderr, "vsk_sound_init failed\n");
        return 1;
    }

    vsk_sound_sing(str);
    vsk_sound_wait(-1);
    vsk_sound_exit();

    return 0;
}
