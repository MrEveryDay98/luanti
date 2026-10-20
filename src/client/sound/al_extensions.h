// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023 DS

#pragma once

#include "al_helpers.h"

// VoxHack: EFX filters (muffled sounds)
#if !defined(_MSC_VER) && !defined(__APPLE__)
	#include <AL/efx.h>
#endif

namespace sound {

/**
 * Struct for AL and ALC extensions
 */
struct ALExtensions
{
	explicit ALExtensions(const ALCdevice *deviceHandle [[maybe_unused]]);

#ifdef AL_SOFT_direct_channels_remix
	bool have_ext_AL_SOFT_direct_channels_remix = false;
#endif

#ifdef AL_FILTER_LOWPASS
	// VoxHack: EFX low-pass filters, for sounds muffled by walls
	bool have_efx = false;
	LPALGENFILTERS genFilters = nullptr;
	LPALDELETEFILTERS deleteFilters = nullptr;
	LPALFILTERI filteri = nullptr;
	LPALFILTERF filterf = nullptr;
	// ... and EFX effects (acoustics: nhv_acoustics.h)
	LPALGENEFFECTS genEffects = nullptr;
	LPALDELETEEFFECTS deleteEffects = nullptr;
	LPALEFFECTI effecti = nullptr;
	LPALEFFECTF effectf = nullptr;
	LPALEFFECTFV effectfv = nullptr;
	LPALGENAUXILIARYEFFECTSLOTS genAuxiliaryEffectSlots = nullptr;
	LPALDELETEAUXILIARYEFFECTSLOTS deleteAuxiliaryEffectSlots = nullptr;
	LPALAUXILIARYEFFECTSLOTI auxiliaryEffectSloti = nullptr;
	LPALAUXILIARYEFFECTSLOTF auxiliaryEffectSlotf = nullptr;
#endif
};

}
