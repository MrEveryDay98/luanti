// VoxHack: lines spoken live by SAM (see sam_voice.h).

#include "sam_voice.h"
#include "midi_render.h"
#include "sam_speech.h"
#include "log.h"
#include "settings.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace sound {

static const char SAM_PREFIX[] = "nhv_sam:";

bool isSamVoiceName(const std::string &name)
{
	return name.compare(0, sizeof(SAM_PREFIX) - 1, SAM_PREFIX) == 0;
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
#ifdef NHV_HAVE_SAM
	return (float)samSpeak(name).size() / nhvsam::RATE;
#else
	return 0.0f;
#endif
}

std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name)
{
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

	OggFileDecodeInfo info;
	info.name_for_logging = "SAM";
	info.is_stereo = false;
	info.format = AL_FORMAT_MONO16;
	info.bytes_per_sample = 2;
	info.freq = nhvsam::RATE;
	info.length_samples = (ALuint)pcm.size();
	info.length_seconds = (f32)pcm.size() / nhvsam::RATE;
	actionstream << "Audio: SAM said a line (" << info.length_seconds << " s)" << std::endl;
	return openPCMData(info, pcm);
#endif
}

} // namespace sound
