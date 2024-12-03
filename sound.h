#pragma once

#include "types.h"

bool vsk_sound_init(void);
void vsk_sound_exit(void);
bool vsk_sound_is_playing(void);
bool vsk_sound_wait(VskDword milliseconds);
void vsk_sound_stop(void);
void vsk_sound_beep(int i);
bool vsk_sound_is_beeping(void);
const char *vsk_get_openal_error(int error);
void vsk_print_openal_error(int error);

bool vsk_sound_cmd_sing(const VskString& str);
