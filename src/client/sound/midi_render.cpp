// NetHack Voxel: MIDI music rendered with a SoundFont (see midi_render.h).

#include "midi_render.h"
#include "log.h"
#include "settings.h"
#include <algorithm>
#include <vector>

#define TSF_IMPLEMENTATION
#include "tinysoundfont/tsf.h"
#define TML_IMPLEMENTATION
#include "tinysoundfont/tml.h"

namespace sound {

static constexpr int MIDI_RATE = 32000;  // samples per second (stereo)
static constexpr int MIDI_BLOCK = 64;    // samples rendered between MIDI events
static constexpr float MIDI_GAIN_DB = -4.0f;

namespace {

// already rendered: one OpenAL buffer with the whole song
struct SoundDataOpenPCM final : ISoundDataOpen
{
	RAIIALSoundBuffer m_buffer;

	SoundDataOpenPCM(const OggFileDecodeInfo &info, const std::vector<short> &pcm) :
			ISoundDataOpen(info)
	{
		m_buffer = RAIIALSoundBuffer::generate();
		alBufferData(m_buffer.get(), info.format, pcm.data(),
				(ALsizei)(pcm.size() * sizeof(short)), info.freq);
	}

	bool isStreaming() const noexcept override { return false; }

	std::tuple<ALuint, ALuint, ALuint> getOrLoadBufferAt(ALuint offset) override
	{
		if (offset >= m_decode_info.length_samples)
			return {0, m_decode_info.length_samples, 0};
		return {m_buffer.get(), m_decode_info.length_samples, offset};
	}
};

// the SoundFont, loaded once (and again if the setting changes)
tsf *soundfont()
{
	static tsf *sf = nullptr;
	static std::string loaded;
	std::string path = g_settings->exists("nhv_soundfont") ? g_settings->get("nhv_soundfont") : "";
	if (path != loaded) {
		if (sf)
			tsf_close(sf);
		sf = path.empty() ? nullptr : tsf_load_filename(path.c_str());
		loaded = path;
		if (sf)
			tsf_set_output(sf, TSF_STEREO_INTERLEAVED, MIDI_RATE, MIDI_GAIN_DB);
		else if (!path.empty())
			warningstream << "Audio: could not load the SoundFont " << path << std::endl;
	}
	return sf;
}

} // namespace

bool isMidiData(const std::string &data)
{
	return data.size() > 14 && data.compare(0, 4, "MThd") == 0;
}

std::shared_ptr<ISoundDataOpen> openMidi(const std::string &data, const std::string &name)
{
	tsf *sf = soundfont();
	if (!sf) {
		warningstream << "Audio: " << name << " is MIDI music, but there's no SoundFont "
				"(setting nhv_soundfont)" << std::endl;
		return nullptr;
	}
	tml_message *midi = tml_load_memory(data.data(), (int)data.size());
	if (!midi) {
		warningstream << "Audio: " << name << " isn't a readable MIDI file" << std::endl;
		return nullptr;
	}
	unsigned int length_ms = 0;
	tml_get_info(midi, nullptr, nullptr, nullptr, nullptr, &length_ms);
	// (a little room for the last notes to ring out)
	size_t total = (size_t)((length_ms + 500) / 1000.0 * MIDI_RATE);
	total = std::max<size_t>(total, MIDI_BLOCK);
	std::vector<short> pcm(total * 2);

	tsf_reset(sf);
	tsf_channel_set_bank_preset(sf, 9, 128, 0); // channel 10: drums
	double msec = 0.0;
	tml_message *m = midi;
	for (size_t pos = 0; pos < total; pos += MIDI_BLOCK) {
		int n = (int)std::min<size_t>(MIDI_BLOCK, total - pos);
		msec += n * 1000.0 / MIDI_RATE;
		for (; m && msec >= m->time; m = m->next) {
			switch (m->type) {
			case TML_PROGRAM_CHANGE:
				tsf_channel_set_presetnumber(sf, m->channel, m->program, m->channel == 9);
				break;
			case TML_NOTE_ON:
				tsf_channel_note_on(sf, m->channel, m->key, m->velocity / 127.0f);
				break;
			case TML_NOTE_OFF:
				tsf_channel_note_off(sf, m->channel, m->key);
				break;
			case TML_PITCH_BEND:
				tsf_channel_set_pitchwheel(sf, m->channel, m->pitch_bend);
				break;
			case TML_CONTROL_CHANGE:
				tsf_channel_midi_control(sf, m->channel, m->control, m->control_value);
				break;
			default:
				break;
			}
		}
		tsf_render_short(sf, &pcm[pos * 2], n, 0);
	}
	tsf_note_off_all(sf);
	tml_free(midi);

	OggFileDecodeInfo info;
	info.name_for_logging = name;
	info.is_stereo = true;
	info.format = AL_FORMAT_STEREO16;
	info.bytes_per_sample = 4;
	info.freq = MIDI_RATE;
	info.length_samples = (ALuint)total;
	info.length_seconds = (f32)total / MIDI_RATE;
	actionstream << "Audio: rendered MIDI " << name << " (" << info.length_seconds << " s)" << std::endl;
	return std::make_shared<SoundDataOpenPCM>(info, pcm);
}

} // namespace sound
