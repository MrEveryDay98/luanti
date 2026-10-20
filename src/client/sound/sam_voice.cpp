// VoxHack: lines spoken live by SAM (see sam_voice.h).

#include "sam_voice.h"
#include "midi_render.h"
#include "log.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef NHV_HAVE_SAM
extern "C" int nhv_sam_render(const char *text, int phonetic, int spd, int ptch, int thrt, int mth,
		int sing, unsigned char **out);
#endif

namespace sound {

static const char SAM_PREFIX[] = "nhv_sam:";
static constexpr int SAM_RATE = 22050;
static constexpr size_t SAM_PIECE = 70;  // characters per piece (SAM's buffers hold ~255 phonemes)

bool isSamVoiceName(const std::string &name)
{
	return name.compare(0, sizeof(SAM_PREFIX) - 1, SAM_PREFIX) == 0;
}

#ifdef NHV_HAVE_SAM
namespace {

bool hasWord(const std::string &s)
{
	return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c); });
}

std::string trim(const std::string &s, const char *chars = " -")
{
	size_t a = s.find_first_not_of(chars), b = s.find_last_not_of(chars);
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// SAM reads short pieces: sentences, then clauses, then words. Anything it
// can't say (accents, symbols) becomes a space; a dash becomes a pause.
std::vector<std::string> pieces(const std::string &text)
{
	std::string clean;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = text[i];
		if (c == 0xE2 && i + 2 < text.size() && (unsigned char)text[i + 1] == 0x80) {
			unsigned char d = text[i + 2];  // UTF-8 punctuation
			i += 2;
			if (d == 0x98 || d == 0x99)
				clean += '\'';
			else if (d == 0x93 || d == 0x94)
				clean += ", ";
			else if (d == 0xA6)
				clean += "...";
			else
				clean += ' ';
		} else if (std::isalnum(c) || std::strchr(" .,!?'-:;\n", c)) {
			clean += (char)c;
		} else {
			clean += ' ';
		}
	}
	std::vector<std::string> sentences, out;
	std::string cur;
	for (size_t i = 0; i < clean.size(); i++) {
		char c = clean[i];
		if (c == '\n') {
			sentences.push_back(cur), cur.clear();
			continue;
		}
		cur += c;
		if ((c == '.' || c == '!' || c == '?') && (i + 1 >= clean.size() || !std::strchr(".!?", clean[i + 1])))
			sentences.push_back(cur), cur.clear();
	}
	sentences.push_back(cur);
	for (std::string s : sentences) {
		s = trim(s, " -:;");
		if (!hasWord(s))
			continue;
		std::vector<std::string> parts;
		if (s.size() <= SAM_PIECE) {
			parts.push_back(s);
		} else {
			std::string p;
			for (char c : s) {
				p += c;
				if (c == ',' || c == ';' || c == ':')
					parts.push_back(p), p.clear();
			}
			parts.push_back(p);
		}
		for (std::string p : parts) {
			p = trim(p);
			while (p.size() > SAM_PIECE) {
				size_t cut = p.rfind(' ', SAM_PIECE);
				if (cut == std::string::npos || cut == 0)
					cut = SAM_PIECE;
				out.push_back(trim(p.substr(0, cut)));
				p = trim(p.substr(cut));
			}
			if (hasWord(p))
				out.push_back(p);
		}
	}
	return out;
}

} // namespace
#endif

std::shared_ptr<ISoundDataOpen> openSamVoice(const std::string &name)
{
#ifndef NHV_HAVE_SAM
	static bool warned = false;
	if (!warned)
		warningstream << "Audio: a line is set to be spoken by SAM, but this build has no SAM" << std::endl;
	warned = true;
	return nullptr;
#else
	size_t bar = name.find('|');
	if (bar == std::string::npos)
		return nullptr;
	int v[6] = {72, 64, 128, 128, 0, 0};  // speed, pitch, throat, mouth, sing, phonetic
	{
		std::istringstream ss(name.substr(sizeof(SAM_PREFIX) - 1, bar - (sizeof(SAM_PREFIX) - 1)));
		std::string f;
		for (int i = 0; i < 6 && std::getline(ss, f, ','); i++)
			v[i] = std::atoi(f.c_str());
	}
	for (int i = 0; i < 4; i++)
		v[i] = std::clamp(v[i], 1, 255);
	std::string text = name.substr(bar + 1);
	std::vector<std::string> list = v[5] ? std::vector<std::string>{trim(text, " ")} : pieces(text);

	std::vector<short> pcm;
	const size_t gap = SAM_RATE * 8 / 100;  // a short breath between pieces
	for (const std::string &piece : list) {
		if (piece.empty())
			continue;
		unsigned char *buf = nullptr;
		int n = nhv_sam_render(piece.c_str(), v[5], v[0], v[1], v[2], v[3], v[4], &buf);
		if (n <= 0) {
			warningstream << "Audio: SAM couldn't read \"" << piece << "\"" << std::endl;
			continue;
		}
		if (!pcm.empty())
			pcm.insert(pcm.end(), gap, 0);
		for (int i = 0; i < n; i++)
			pcm.push_back((short)(((int)buf[i] - 128) * 256));
		std::free(buf);
	}
	if (pcm.empty())
		return nullptr;

	OggFileDecodeInfo info;
	info.name_for_logging = "SAM";
	info.is_stereo = false;
	info.format = AL_FORMAT_MONO16;
	info.bytes_per_sample = 2;
	info.freq = SAM_RATE;
	info.length_samples = (ALuint)pcm.size();
	info.length_seconds = (f32)pcm.size() / SAM_RATE;
	actionstream << "Audio: SAM read " << list.size() << " piece(s) (" << info.length_seconds
			<< " s)" << std::endl;
	return openPCMData(info, pcm);
#endif
}

} // namespace sound
