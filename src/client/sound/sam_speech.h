// VoxHack: what SAM says and how it sounds.
//
// Turns a line of text into SAM's speech: the pronunciation dictionary,
// numbers and symbols read as words, {PHONEMES} written straight into the
// text, short pieces (SAM's buffers are small), quicker and higher
// exclamations, soft edges between pieces, and an optional smoothing of
// SAM's 8-bit sound. Used by the engine (sam_voice.cpp) and by the sound
// editor's preview tool (VoxHack's tools/samtool.cpp), so both say exactly
// the same. No engine dependencies.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace nhvsam {

constexpr int RATE = 22050;  // SAM's sample rate

struct Settings {
	int speed = 72, pitch = 64, throat = 128, mouth = 128;  // SAM's own (1-255)
	bool sing = false;      // SAM's sing mode: the pitch follows the phonemes
	bool phonetic = false;  // the whole text is SAM phonemes
	bool smooth = false;    // soften SAM's 8-bit edge
	bool tone = true;       // exclamations are quicker and higher
};

// lowercase word -> what SAM says instead: other words, or {PHONEMES}
using Dictionary = std::unordered_map<std::string, std::string>;

// "word = replacement" lines; '#' starts a comment
Dictionary parseDictionary(const std::string &text);

// the pieces SAM will read, as phonemes (to show, and to copy into {...})
std::vector<std::string> toPhonemes(const std::string &text, const Settings &s, const Dictionary *dict);

// 16-bit mono PCM at RATE; empty if there was nothing SAM could say
std::vector<short> speak(const std::string &text, const Settings &s, const Dictionary *dict);

} // namespace nhvsam
