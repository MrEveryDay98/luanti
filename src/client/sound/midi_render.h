// NetHack Voxel: MIDI music.
//
// A sound file that is a Standard MIDI File (it starts with "MThd"; the game
// sends .mid / .midi files like .ogg ones) is rendered to stereo PCM with a
// General MIDI SoundFont when it's first played, then plays like any other
// sound (looping, fading, gain). The SoundFont is the .sf2 file named by the
// setting "nhv_soundfont" (the game sets it). Uses TinySoundFont
// (tinysoundfont/, MIT licence).

#pragma once

#include "sound_data.h"
#include <memory>
#include <string>

namespace sound {

bool isMidiData(const std::string &data);

// nullptr if it can't be rendered (no SoundFont, a broken file...)
std::shared_ptr<ISoundDataOpen> openMidi(const std::string &data, const std::string &name);

} // namespace sound
