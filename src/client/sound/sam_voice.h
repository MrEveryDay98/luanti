// VoxHack: lines spoken live by SAM (Software Automatic Mouth).
//
// A sound played with a name like
//     nhv_sam:<speed>,<pitch>,<throat>,<mouth>,<sing 0|1>,<phonetic 0|1>|<text>
// isn't a file: the sound manager has SAM read the text there and then (in
// short pieces, as SAM's buffers are small) and plays the result like any
// other sound (gain, position, pitch...). SAM is built in only when its
// source was found at build time (CMake NHV_SAM_SRC, see sam_unity.c);
// otherwise these sounds are silent.

#pragma once

#include "sound_data.h"
#include <memory>
#include <string>

namespace sound {

bool isSamVoiceName(const std::string &name);

// nullptr if SAM isn't built in or can't read the text
std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name);

} // namespace sound
