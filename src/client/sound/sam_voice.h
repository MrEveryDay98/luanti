// VoxHack: lines spoken live by SAM (Software Automatic Mouth).
//
// A sound played with a name like
//     nhv_sam:<speed>,<pitch>,<throat>,<mouth>,<sing>,<phonetic>,<smooth>,<tone>,<effects>
//             [,<hifi>[,<brightness>,<breath>,<pitch movement>,<hiss>[,<whisper>]]]|<text>
// (flags 0|1; effects like "echo:delay=250/reverb:room=60", may be empty;
// hifi: sam_hifi.h, tuned in percent, 100 as made)
// isn't a file: the sound manager has SAM read the text there and then
// (sam_speech.h: the pronunciation dictionary named by the setting
// nhv_sam_dict, numbers as words...) and plays the result like any
// other sound (gain, position, pitch...). SAM's source is lib/sam (CMake
// NHV_SAM_SRC, see sam_unity.c); a build without it leaves these silent.
//
// Or the line said in Animalese, the voice a player may choose over SAM:
//     nhv_animalese:<pitch>,<length>,<shorten>,<effects>|<text>
// every letter a short sound of its own, said quickly one after another
// (Josh Simmons' animalese.js, github.com/Acedio/animalese.js, MIT, done
// again here) from the game's mods/nhv_sounds/animalese/animalese.wav (CC
// BY 4.0): pitch and letter length in percent (100: as recorded, 75 ms a
// letter), shorten 0|1 (each word only its first and last letters), the
// effects as SAM's (they need SAM built in).

#pragma once

#include "sound_data.h"
#include <memory>
#include <string>

namespace sound {

// a line for SAM or Animalese to say
bool isSamVoiceName(const std::string &name);

// nullptr if SAM isn't built in (or Animalese has no letters) or can't read the text
std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name);

// how long that line takes to say, in seconds (0: it can't be said)
float samVoiceSeconds(const std::string &name);

} // namespace sound
