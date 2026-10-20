// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023 DS

#include "al_extensions.h"

#include "settings.h"
#include "util/string.h"
#include <unordered_set>

namespace sound {

ALExtensions::ALExtensions(const ALCdevice *deviceHandle [[maybe_unused]])
{
	auto blacklist_vec = str_split(g_settings->get("sound_extensions_blacklist"), ',');
	for (auto &s : blacklist_vec) {
		s = trim(s);
	}
	std::unordered_set<std::string> blacklist;
	blacklist.insert(blacklist_vec.begin(), blacklist_vec.end());

	{
		constexpr const char *ext_name = "AL_SOFT_direct_channels_remix";
		bool blacklisted = blacklist.find(ext_name) != blacklist.end();
		if (blacklisted)
			infostream << "ALExtensions: Blacklisted: " << ext_name << std::endl;
#ifndef AL_SOFT_direct_channels_remix
		infostream << "ALExtensions: Not compiled with: " << ext_name << std::endl;
#else
		bool found = alIsExtensionPresent(ext_name);
		if (found)
			infostream << "ALExtensions: Found: " << ext_name << std::endl;
		else
			infostream << "ALExtensions: Not found: " << ext_name << std::endl;

		if (found && !blacklisted) {
			have_ext_AL_SOFT_direct_channels_remix = true;
		}
#endif
	}

#ifdef AL_FILTER_LOWPASS
	// VoxHack: EFX (muffled sounds)
	if (deviceHandle && alcIsExtensionPresent(const_cast<ALCdevice *>(deviceHandle), "ALC_EXT_EFX")) {
		genFilters = (LPALGENFILTERS)alGetProcAddress("alGenFilters");
		deleteFilters = (LPALDELETEFILTERS)alGetProcAddress("alDeleteFilters");
		filteri = (LPALFILTERI)alGetProcAddress("alFilteri");
		filterf = (LPALFILTERF)alGetProcAddress("alFilterf");
		have_efx = genFilters && deleteFilters && filteri && filterf;
		genEffects = (LPALGENEFFECTS)alGetProcAddress("alGenEffects");
		deleteEffects = (LPALDELETEEFFECTS)alGetProcAddress("alDeleteEffects");
		effecti = (LPALEFFECTI)alGetProcAddress("alEffecti");
		effectf = (LPALEFFECTF)alGetProcAddress("alEffectf");
		effectfv = (LPALEFFECTFV)alGetProcAddress("alEffectfv");
		genAuxiliaryEffectSlots = (LPALGENAUXILIARYEFFECTSLOTS)alGetProcAddress("alGenAuxiliaryEffectSlots");
		deleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS)alGetProcAddress("alDeleteAuxiliaryEffectSlots");
		auxiliaryEffectSloti = (LPALAUXILIARYEFFECTSLOTI)alGetProcAddress("alAuxiliaryEffectSloti");
		auxiliaryEffectSlotf = (LPALAUXILIARYEFFECTSLOTF)alGetProcAddress("alAuxiliaryEffectSlotf");
		if (!(genEffects && deleteEffects && effecti && effectf && effectfv && genAuxiliaryEffectSlots
				&& deleteAuxiliaryEffectSlots && auxiliaryEffectSloti && auxiliaryEffectSlotf))
			genEffects = nullptr; // (no acoustics, filters still work)
	}
	infostream << "ALExtensions: EFX filters " << (have_efx ? "found" : "not found") << std::endl;
#endif
}

}
