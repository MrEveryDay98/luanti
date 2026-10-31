// VoxHack: lines spoken live by SAM (see sam_voice.h).

#include "sam_voice.h"
#include "midi_render.h"
#include "sam_speech.h"
#include "log.h"
#include "settings.h"
#include "filesys.h"
#include "porting.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace sound {

static const char SAM_PREFIX[] = "nhv_sam:";
static const char ANIMALESE_PREFIX[] = "nhv_animalese:";

static bool isAnimaleseName(const std::string &name)
{
	return name.compare(0, sizeof(ANIMALESE_PREFIX) - 1, ANIMALESE_PREFIX) == 0;
}

bool isSamVoiceName(const std::string &name)
{
	return name.compare(0, sizeof(SAM_PREFIX) - 1, SAM_PREFIX) == 0 || isAnimaleseName(name);
}

// ---------------------------------------------------------------- Animalese
// (see sam_voice.h) Its letters: A to Z one after another, 0.15 s each, in
// a WAV file (8 or 16 bit, mono), read again whenever it changes
struct AnimaleseLetters {
	std::vector<float> pcm;  // -1..1
	int rate = 0;
	size_t slot = 0;         // samples a letter
};

static bool readWav(const std::string &data, AnimaleseLetters &out)
{
	if (data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0)
		return false;
	auto u16 = [&](size_t at) { return (unsigned)(unsigned char)data[at] | (unsigned)(unsigned char)data[at + 1] << 8; };
	auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
	int channels = 0, bits = 0;
	size_t at = 12;
	while (at + 8 <= data.size()) {
		size_t len = u32(at + 4), body = at + 8;
		len = std::min(len, data.size() - body);
		if (data.compare(at, 4, "fmt ") == 0 && len >= 16) {
			if (u16(body) != 1)  // (PCM only)
				return false;
			channels = u16(body + 2), out.rate = (int)u32(body + 4), bits = u16(body + 14);
		} else if (data.compare(at, 4, "data") == 0 && channels > 0) {
			int bytes = bits / 8;
			if ((bits != 8 && bits != 16) || out.rate <= 0)
				return false;
			size_t frame = (size_t)bytes * channels;
			for (size_t i = body; i + frame <= body + len; i += frame) {
				// (the first channel)
				out.pcm.push_back(bits == 8 ? ((unsigned char)data[i] - 128) / 128.0f
						: (short)u16(i) / 32768.0f);
			}
			out.slot = (size_t)(0.15 * out.rate);
			return out.slot > 0 && !out.pcm.empty();
		}
		at = body + len + (len & 1);
	}
	return false;
}

static const AnimaleseLetters *animaleseLetters()
{
	static AnimaleseLetters letters;
	static std::filesystem::file_time_type loaded_time;
	static bool tried = false;
	std::string path = porting::path_share + DIR_DELIM + "games" + DIR_DELIM + "nethackvoxel" + DIR_DELIM
			+ "mods" + DIR_DELIM + "nhv_sounds" + DIR_DELIM + "animalese" + DIR_DELIM + "animalese.wav";
	std::error_code ec;
	auto t = std::filesystem::last_write_time(path, ec);
	if (!tried || (!ec && t != loaded_time)) {
		tried = true;
		loaded_time = t;
		letters = AnimaleseLetters();
		std::ifstream f(path, std::ios::binary);
		std::ostringstream ss;
		if (f)
			ss << f.rdbuf();
		if (!readWav(ss.str(), letters)) {
			letters = AnimaleseLetters();
			warningstream << "Audio: no Animalese letters in " << path << std::endl;
		}
	}
	return letters.pcm.empty() ? nullptr : &letters;
}

// what's said, letter by letter: 'A'..'Z', or ' ' a letter's silence
static std::string animaleseScript(const std::string &text, bool shorten)
{
	static const char *const DIGITS[] = {"zero", "one", "two", "three", "four", "five", "six", "seven",
			"eight", "nine"};
	std::string out, word;
	auto endWord = [&]() {
		if (shorten && word.size() > 1)
			word = std::string(1, word.front()) + word.back();
		out += word;
		word.clear();
	};
	auto gap = [&](size_t n) {
		endWord();
		// (no gap at the start, and none piled on another: the longer)
		if (out.empty())
			return;
		size_t have = 0;
		while (have < out.size() && out[out.size() - 1 - have] == ' ')
			have++;
		for (; have < n; have++)
			out += ' ';
	};
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if (c == '{') {
			// (SAM's phonemes: not for Animalese)
			size_t e = text.find('}', i);
			i = e == std::string::npos ? text.size() : e;
		} else if (std::isalpha((unsigned char)c)) {
			word += (char)std::toupper((unsigned char)c);
		} else if (std::isdigit((unsigned char)c)) {
			for (const char *d = DIGITS[c - '0']; *d; d++)
				word += (char)std::toupper((unsigned char)*d);
		} else if (c == '\'' && !word.empty()) {
			// (don't, it's: one word)
		} else if (c == '.' || c == '!' || c == '?') {
			gap(3);
		} else if (c == ',' || c == ';' || c == ':' || c == '-') {
			gap(2);
		} else if (!shorten) {
			gap(1);  // (shortened, the words run together)
		} else {
			endWord();
		}
	}
	endWord();
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

static std::vector<short> animaleseSpeak(const std::string &name)
{
	size_t bar = name.find('|');
	const AnimaleseLetters *lib = animaleseLetters();
	if (bar == std::string::npos || !lib)
		return {};
	// pitch, length (percent), shorten, then the effect chain
	int v[3] = {100, 100, 0};
	std::string effects;
	{
		std::istringstream ss(name.substr(sizeof(ANIMALESE_PREFIX) - 1, bar - (sizeof(ANIMALESE_PREFIX) - 1)));
		std::string f;
		for (int i = 0; i < 4 && std::getline(ss, f, ','); i++) {
			if (i < 3)
				v[i] = std::atoi(f.c_str());
			else
				effects = f;
		}
	}
	std::string script = animaleseScript(name.substr(bar + 1), v[2] != 0);
	if (script.find_first_not_of(' ') == std::string::npos)
		return {};
	const int out_rate = nhvsam::RATE;
	// each letter read faster (higher) or slower (lower) than recorded, for
	// 75 ms (its length in percent), never past its own sound
	float step = std::clamp(v[0], 25, 400) / 100.0f * lib->rate / out_rate;
	size_t letter = (size_t)(0.075 * out_rate * std::clamp(v[1], 25, 400) / 100.0f);
	const size_t fade = (size_t)(out_rate * 3 / 1000);
	std::vector<float> x;
	x.reserve(script.size() * letter);
	for (char c : script) {
		size_t at = x.size();
		x.resize(at + letter, 0.0f);
		if (c < 'A' || c > 'Z')
			continue;
		size_t start = lib->slot * (size_t)(c - 'A');
		size_t n = std::min(letter, (size_t)((lib->slot - 1) / step));
		for (size_t i = 0; i < n; i++) {
			float pos = i * step;
			size_t k = (size_t)pos;
			float fr = pos - k;
			size_t a = std::min(start + k, lib->pcm.size() - 1), b = std::min(a + 1, lib->pcm.size() - 1);
			float s = lib->pcm[a] * (1 - fr) + lib->pcm[b] * fr;
			// (soft edges: no click between letters)
			float g = std::min(1.0f, std::min((float)i / fade, (float)(n - 1 - i) / fade));
			x[at + i] = s * g * 26000.0f;
		}
	}
#ifdef NHV_HAVE_SAM
	if (!effects.empty())
		nhvsam::applyEffectChain(x, effects);
#endif
	std::vector<short> pcm(x.size());
	for (size_t i = 0; i < x.size(); i++)
		pcm[i] = (short)std::clamp(x[i], -32768.0f, 32767.0f);
	return pcm;
}

#ifdef NHV_HAVE_SAM
// the pronunciation dictionary (the file the setting nhv_sam_dict names),
// read again whenever it changes
static const nhvsam::Dictionary *dictionary()
{
	static nhvsam::Dictionary dict;
	static std::string loaded_path;
	static std::filesystem::file_time_type loaded_time;
	std::string path = g_settings->exists("nhv_sam_dict") ? g_settings->get("nhv_sam_dict") : "";
	std::error_code ec;
	auto t = path.empty() ? std::filesystem::file_time_type() : std::filesystem::last_write_time(path, ec);
	if (path != loaded_path || (!ec && t != loaded_time)) {
		dict.clear();
		std::ifstream f(path, std::ios::binary);
		if (f) {
			std::ostringstream ss;
			ss << f.rdbuf();
			dict = nhvsam::parseDictionary(ss.str());
		}
		loaded_path = path;
		loaded_time = ec ? std::filesystem::file_time_type() : t;
	}
	return &dict;
}
#endif

#ifdef NHV_HAVE_SAM
// the line a name holds, said: its PCM (empty if it can't be)
static std::vector<short> samSpeak(const std::string &name)
{
	size_t bar = name.find('|');
	if (bar == std::string::npos)
		return {};
	// speed, pitch, throat, mouth, sing, phonetic, smooth, tone, then the
	// effect chain (sam_speech.h), then hi-fi (left out: off) and its tuning
	// in percent (brightness, breath, pitch movement, hiss; left out: 100)
	// and whispered (0|1)
	int v[8] = {72, 64, 128, 128, 0, 0, 0, 1};
	int tune[5] = {100, 100, 100, 100, 0};
	std::string effects;
	bool hifi = false;
	{
		std::istringstream ss(name.substr(sizeof(SAM_PREFIX) - 1, bar - (sizeof(SAM_PREFIX) - 1)));
		std::string f;
		for (int i = 0; i < 15 && std::getline(ss, f, ','); i++) {
			if (i < 8)
				v[i] = std::atoi(f.c_str());
			else if (i == 8)
				effects = f;
			else if (i == 9)
				hifi = std::atoi(f.c_str()) != 0;
			else
				tune[i - 10] = std::atoi(f.c_str());
		}
	}
	nhvsam::Settings s;
	s.effects = effects;
	s.hifi = hifi;
	s.bright = tune[0] / 100.0f, s.breath = tune[1] / 100.0f, s.movement = tune[2] / 100.0f;
	s.hiss = tune[3] / 100.0f;
	s.whisper = tune[4] != 0;
	s.speed = v[0], s.pitch = v[1], s.throat = v[2], s.mouth = v[3];
	s.sing = v[4], s.phonetic = v[5], s.smooth = v[6], s.tone = v[7];
	return nhvsam::speak(name.substr(bar + 1), s, dictionary());
}
#endif

float samVoiceSeconds(const std::string &name)
{
	if (isAnimaleseName(name))
		return (float)animaleseSpeak(name).size() / nhvsam::RATE;
#ifdef NHV_HAVE_SAM
	return (float)samSpeak(name).size() / nhvsam::RATE;
#else
	return 0.0f;
#endif
}

// a line's sound, as any other's
static std::shared_ptr<ISoundDataOpen> openVoicePCM(const char *who, const std::vector<short> &pcm)
{
	OggFileDecodeInfo info;
	info.name_for_logging = who;
	info.is_stereo = false;
	info.format = AL_FORMAT_MONO16;
	info.bytes_per_sample = 2;
	info.freq = nhvsam::RATE;
	info.length_samples = (ALuint)pcm.size();
	info.length_seconds = (f32)pcm.size() / nhvsam::RATE;
	actionstream << "Audio: " << who << " said a line (" << info.length_seconds << " s)" << std::endl;
	return openPCMData(info, pcm);
}

std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name)
{
	if (isAnimaleseName(name)) {
		std::vector<short> pcm = animaleseSpeak(name);
		return pcm.empty() ? nullptr : openVoicePCM("Animalese", pcm);
	}
#ifndef NHV_HAVE_SAM
	static bool warned = false;
	if (!warned)
		warningstream << "Audio: a line is set to be spoken by SAM, but this build has no SAM" << std::endl;
	warned = true;
	return nullptr;
#else
	std::vector<short> pcm = samSpeak(name);
	if (pcm.empty()) {
		warningstream << "Audio: SAM couldn't say \"" << name.substr(name.find('|') + 1) << "\"" << std::endl;
		return nullptr;
	}
	return openVoicePCM("SAM", pcm);
#endif
}

} // namespace sound
