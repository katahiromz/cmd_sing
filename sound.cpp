#include "types.h"
#include "sound.h"
#include "encoding.h"
#include "ast.h"
#include <cstdio>
#include <cassert>

#include "freealut/include/AL/alut.h"
#include "fmgon/soundplayer.h"
#include "scanner.h"

std::shared_ptr<VskSoundPlayer> vsk_sound_player;

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

const char *vsk_get_openal_error(int error)
{
    switch (error)
    {
    case AL_NO_ERROR:           return "No Error";
    case AL_INVALID_NAME:       return "Invalid Name";
    case AL_INVALID_ENUM:       return "Invalid Enum";
    case AL_INVALID_VALUE:      return "Invalid Value";
    case AL_INVALID_OPERATION:  return "Invalid Operation";
    case AL_OUT_OF_MEMORY:      return "Out of Memory";
    default:                    return "Unknown Error";
    }
}

void vsk_print_openal_error(int error)
{
    std::printf(vsk_get_openal_error(error));
}
