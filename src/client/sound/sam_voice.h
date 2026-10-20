// VoxHack: lines spoken live by SAM (Software Automatic Mouth).
//
// A sound played with a name like
//     nhv_sam:<speed>,<pitch>,<throat>,<mouth>,<sing>,<phonetic>,<smooth>,<tone>|<text>
// (flags 0|1)
// isn't a file: the sound manager has SAM read the text there and then
// (sam_speech.h: the pronunciation dictionary named by the setting
// nhv_sam_dict, numbers as words...) and plays the result like any
// other sound (gain, position, pitch...). SAM's source is lib/sam (CMake
// NHV_SAM_SRC, see sam_unity.c); a build without it leaves these silent.

#pragma once

#include "sound_data.h"
#include <memory>
#include <string>

namespace sound {

bool isSamVoiceName(const std::string &name);

// nullptr if SAM isn't built in or can't read the text
std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name);

} // namespace sound
