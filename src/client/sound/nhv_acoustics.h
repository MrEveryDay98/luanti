// VoxHack: acoustics, the reverb of the place the listener is in.
//
// Every sound sends some of itself (its nhv_reverb, 0..1: music sends
// none) to one OpenAL EFX reverb. What the reverb sounds like comes from
// the setting "nhv_acoustics", which the game updates as the hero moves:
//     preset=CAVE;decay=1.3;wet=0.45
// preset: one of OpenAL's reverb presets (efx-presets.h, e.g. CAVE,
// HALLWAY, CHAPEL, UNDERWATER; NONE: dry); decay: its decay time,
// multiplied (bigger spaces ring longer); wet: how loud the reverb is.
// Changes glide over a moment rather than jumping.

#pragma once

#include "al_extensions.h"
#include <string>

namespace sound {

class NhvAcoustics
{
public:
	explicit NhvAcoustics(const ALExtensions &exts);
	~NhvAcoustics();

	// follow the setting, gliding toward it
	void step(f32 dtime);
	// a new source sends this much (0..1) of itself to the reverb
	void attach(ALuint source, f32 amount);

private:
	const ALExtensions &m_exts;
	bool m_ok = false;
	bool m_eax = false;
	ALuint m_effect = 0, m_slot = 0;
	std::string m_setting;
	f32 m_check = 0.0f;
	// current and target reverb (EFXEAXREVERBPROPERTIES as floats) and wet
	static constexpr int N = 27;
	float m_cur[N] = {}, m_target[N] = {};
	float m_wet = 0.0f, m_target_wet = 0.0f;
	bool m_settled = true;

	void apply();
};

} // namespace sound
