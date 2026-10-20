// VoxHack: acoustics (see nhv_acoustics.h).

#include "nhv_acoustics.h"
#include "log.h"
#include "settings.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef AL_FILTER_LOWPASS
#include <AL/efx-presets.h>
#endif

namespace sound {

#ifdef AL_FILTER_LOWPASS
namespace {

struct Preset {
	const char *name;
	EFXEAXREVERBPROPERTIES p;
};

const Preset PRESETS[] = {
	{"GENERIC", EFX_REVERB_PRESET_GENERIC},
	{"ROOM", EFX_REVERB_PRESET_ROOM},
	{"LIVINGROOM", EFX_REVERB_PRESET_LIVINGROOM},
	{"STONEROOM", EFX_REVERB_PRESET_STONEROOM},
	{"AUDITORIUM", EFX_REVERB_PRESET_AUDITORIUM},
	{"CONCERTHALL", EFX_REVERB_PRESET_CONCERTHALL},
	{"CAVE", EFX_REVERB_PRESET_CAVE},
	{"ARENA", EFX_REVERB_PRESET_ARENA},
	{"HANGAR", EFX_REVERB_PRESET_HANGAR},
	{"HALLWAY", EFX_REVERB_PRESET_HALLWAY},
	{"STONECORRIDOR", EFX_REVERB_PRESET_STONECORRIDOR},
	{"ALLEY", EFX_REVERB_PRESET_ALLEY},
	{"FOREST", EFX_REVERB_PRESET_FOREST},
	{"CITY", EFX_REVERB_PRESET_CITY},
	{"MOUNTAINS", EFX_REVERB_PRESET_MOUNTAINS},
	{"QUARRY", EFX_REVERB_PRESET_QUARRY},
	{"PLAIN", EFX_REVERB_PRESET_PLAIN},
	{"PARKINGLOT", EFX_REVERB_PRESET_PARKINGLOT},
	{"SEWERPIPE", EFX_REVERB_PRESET_SEWERPIPE},
	{"UNDERWATER", EFX_REVERB_PRESET_UNDERWATER},
	{"DRUGGED", EFX_REVERB_PRESET_DRUGGED},
	{"DIZZY", EFX_REVERB_PRESET_DIZZY},
	{"PSYCHOTIC", EFX_REVERB_PRESET_PSYCHOTIC},
	{"CASTLE_LARGEROOM", EFX_REVERB_PRESET_CASTLE_LARGEROOM},
	{"CASTLE_HALL", EFX_REVERB_PRESET_CASTLE_HALL},
	{"CASTLE_CUPBOARD", EFX_REVERB_PRESET_CASTLE_CUPBOARD},
	{"CASTLE_ALCOVE", EFX_REVERB_PRESET_CASTLE_ALCOVE},
	{"WOODEN_SMALLROOM", EFX_REVERB_PRESET_WOODEN_SMALLROOM},
	{"PREFAB_WORKSHOP", EFX_REVERB_PRESET_PREFAB_WORKSHOP},
	{"DOME_TOMB", EFX_REVERB_PRESET_DOME_TOMB},
	{"PIPE_LARGE", EFX_REVERB_PRESET_PIPE_LARGE},
	{"MOOD_HEAVEN", EFX_REVERB_PRESET_MOOD_HEAVEN},
	{"MOOD_HELL", EFX_REVERB_PRESET_MOOD_HELL},
	{"DUSTYROOM", EFX_REVERB_PRESET_DUSTYROOM},
	{"CHAPEL", EFX_REVERB_PRESET_CHAPEL},
	{"SMALLWATERROOM", EFX_REVERB_PRESET_SMALLWATERROOM},
};

// the preset as 27 floats (its last field, an int, as a float too)
void unpack(const EFXEAXREVERBPROPERTIES &p, float *out)
{
	static_assert(sizeof(EFXEAXREVERBPROPERTIES) == 27 * sizeof(float), "layout");
	std::memcpy(out, &p, 26 * sizeof(float));
	out[26] = (float)p.iDecayHFLimit;
}

} // namespace
#endif

NhvAcoustics::NhvAcoustics(const ALExtensions &exts) : m_exts(exts)
{
#ifdef AL_FILTER_LOWPASS
	if (!exts.have_efx || !exts.genEffects || !exts.genAuxiliaryEffectSlots)
		return;
	exts.genEffects(1, &m_effect);
	alGetError();
	exts.effecti(m_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
	m_eax = alGetError() == AL_NO_ERROR;
	if (!m_eax)
		exts.effecti(m_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
	exts.genAuxiliaryEffectSlots(1, &m_slot);
	exts.auxiliaryEffectSlotf(m_slot, AL_EFFECTSLOT_GAIN, 0.0f);
	m_ok = alGetError() == AL_NO_ERROR;
	unpack(PRESETS[0].p, m_cur);
	std::memcpy(m_target, m_cur, sizeof(m_cur));
	if (m_ok)
		apply();
	infostream << "Audio: acoustics " << (m_ok ? (m_eax ? "(EAX reverb)" : "(reverb)") : "unavailable")
			<< std::endl;
#endif
}

NhvAcoustics::~NhvAcoustics()
{
#ifdef AL_FILTER_LOWPASS
	if (m_slot)
		m_exts.deleteAuxiliaryEffectSlots(1, &m_slot);
	if (m_effect)
		m_exts.deleteEffects(1, &m_effect);
#endif
}

void NhvAcoustics::apply()
{
#ifdef AL_FILTER_LOWPASS
	const float *c = m_cur;
	auto f = [&](ALenum e, float v) { m_exts.effectf(m_effect, e, v); };
	auto clampf = [](float v, float lo, float hi) { return std::fmax(lo, std::fmin(hi, v)); };
	if (m_eax) {
		f(AL_EAXREVERB_DENSITY, c[0]);
		f(AL_EAXREVERB_DIFFUSION, c[1]);
		f(AL_EAXREVERB_GAIN, c[2]);
		f(AL_EAXREVERB_GAINHF, c[3]);
		f(AL_EAXREVERB_GAINLF, c[4]);
		f(AL_EAXREVERB_DECAY_TIME, clampf(c[5], 0.1f, 20.0f));
		f(AL_EAXREVERB_DECAY_HFRATIO, c[6]);
		f(AL_EAXREVERB_DECAY_LFRATIO, c[7]);
		f(AL_EAXREVERB_REFLECTIONS_GAIN, c[8]);
		f(AL_EAXREVERB_REFLECTIONS_DELAY, c[9]);
		m_exts.effectfv(m_effect, AL_EAXREVERB_REFLECTIONS_PAN, c + 10);
		f(AL_EAXREVERB_LATE_REVERB_GAIN, c[13]);
		f(AL_EAXREVERB_LATE_REVERB_DELAY, c[14]);
		m_exts.effectfv(m_effect, AL_EAXREVERB_LATE_REVERB_PAN, c + 15);
		f(AL_EAXREVERB_ECHO_TIME, c[18]);
		f(AL_EAXREVERB_ECHO_DEPTH, c[19]);
		f(AL_EAXREVERB_MODULATION_TIME, c[20]);
		f(AL_EAXREVERB_MODULATION_DEPTH, c[21]);
		f(AL_EAXREVERB_AIR_ABSORPTION_GAINHF, c[22]);
		f(AL_EAXREVERB_HFREFERENCE, c[23]);
		f(AL_EAXREVERB_LFREFERENCE, c[24]);
		f(AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, c[25]);
		m_exts.effecti(m_effect, AL_EAXREVERB_DECAY_HFLIMIT, c[26] > 0.5f);
	} else {
		f(AL_REVERB_DENSITY, c[0]);
		f(AL_REVERB_DIFFUSION, c[1]);
		f(AL_REVERB_GAIN, c[2]);
		f(AL_REVERB_GAINHF, c[3]);
		f(AL_REVERB_DECAY_TIME, clampf(c[5], 0.1f, 20.0f));
		f(AL_REVERB_DECAY_HFRATIO, clampf(c[6], 0.1f, 2.0f));
		f(AL_REVERB_REFLECTIONS_GAIN, c[8]);
		f(AL_REVERB_REFLECTIONS_DELAY, c[9]);
		f(AL_REVERB_LATE_REVERB_GAIN, c[13]);
		f(AL_REVERB_LATE_REVERB_DELAY, c[14]);
		f(AL_REVERB_AIR_ABSORPTION_GAINHF, c[22]);
		f(AL_REVERB_ROOM_ROLLOFF_FACTOR, c[25]);
		m_exts.effecti(m_effect, AL_REVERB_DECAY_HFLIMIT, c[26] > 0.5f);
	}
	// (a slot takes a copy of its effect: attach it again after changes)
	m_exts.auxiliaryEffectSloti(m_slot, AL_EFFECTSLOT_EFFECT, (ALint)m_effect);
	m_exts.auxiliaryEffectSlotf(m_slot, AL_EFFECTSLOT_GAIN, m_wet);
	alGetError();
#endif
}

void NhvAcoustics::step(f32 dtime)
{
#ifdef AL_FILTER_LOWPASS
	if (!m_ok)
		return;
	m_check -= dtime;
	if (m_check <= 0.0f) {
		m_check = 0.2f;
		std::string s = g_settings->exists("nhv_acoustics") ? g_settings->get("nhv_acoustics") : "";
		if (s != m_setting) {
			m_setting = s;
			// "preset=CAVE;decay=1.3;wet=0.45"
			std::string preset = "NONE";
			float decay = 1.0f, wet = 0.0f;
			std::istringstream ss(s);
			std::string part;
			while (std::getline(ss, part, ';')) {
				size_t eq = part.find('=');
				if (eq == std::string::npos)
					continue;
				std::string k = part.substr(0, eq), v = part.substr(eq + 1);
				if (k == "preset")
					preset = v;
				else if (k == "decay")
					decay = std::strtof(v.c_str(), nullptr);
				else if (k == "wet")
					wet = std::strtof(v.c_str(), nullptr);
			}
			const Preset *found = nullptr;
			for (const Preset &p : PRESETS)
				if (preset == p.name)
					found = &p;
			if (found) {
				unpack(found->p, m_target);
				m_target[5] *= std::fmax(0.1f, std::fmin(decay, 4.0f));
				m_target_wet = std::fmax(0.0f, std::fmin(wet, 1.0f));
			} else {
				m_target_wet = 0.0f; // NONE (or unknown): dry
			}
			m_settled = false;
		}
	}
	if (m_settled)
		return;
	// glide: most of the way in about half a second
	float k = std::fmin(1.0f, dtime * 5.0f);
	bool done = true;
	for (int i = 0; i < N; i++) {
		float d = m_target[i] - m_cur[i];
		if (std::fabs(d) > 1e-4f * (1.0f + std::fabs(m_target[i]))) {
			m_cur[i] += d * k;
			done = false;
		} else {
			m_cur[i] = m_target[i];
		}
	}
	m_cur[26] = m_target[26];
	float dw = m_target_wet - m_wet;
	if (std::fabs(dw) > 1e-3f) {
		m_wet += dw * k;
		done = false;
	} else {
		m_wet = m_target_wet;
	}
	apply();
	m_settled = done;
#else
	(void)dtime;
#endif
}

void NhvAcoustics::attach(ALuint source, f32 amount)
{
#ifdef AL_FILTER_LOWPASS
	if (!m_ok || !(amount > 0.0f))
		return;
	if (amount >= 0.999f) {
		alSource3i(source, AL_AUXILIARY_SEND_FILTER, (ALint)m_slot, 0, AL_FILTER_NULL);
	} else {
		ALuint filter = 0;
		m_exts.genFilters(1, &filter);
		m_exts.filteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
		m_exts.filterf(filter, AL_LOWPASS_GAIN, amount);
		m_exts.filterf(filter, AL_LOWPASS_GAINHF, 1.0f);
		alSource3i(source, AL_AUXILIARY_SEND_FILTER, (ALint)m_slot, 0, (ALint)filter);
		m_exts.deleteFilters(1, &filter);
	}
	alGetError();
#else
	(void)source;
	(void)amount;
#endif
}

} // namespace sound
