/* VoxHack: SAM (Software Automatic Mouth), built into the engine.
 *
 * It compiles Sebastian Macke's C port of SAM (github.com/s-macke/SAM),
 * kept unchanged in lib/sam (see lib/sam/VOXHACK.md), from the folder CMake's
 * NHV_SAM_SRC points at. All of SAM is one translation unit here, with every one of its
 * global names prefixed, so names like A, X, Y, input or buffer can't clash
 * with anything else in the engine. sam_voice.cpp uses nhv_sam_render().
 */

/* SAM's own includes come first, so the renames below can't touch them */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A nhv_sam_A
#define X nhv_sam_X
#define Y nhv_sam_Y
#define AddInflection nhv_sam_AddInflection
#define AdjustLengths nhv_sam_AdjustLengths
#define Code37055 nhv_sam_Code37055
#define Code37066 nhv_sam_Code37066
#define Code41240 nhv_sam_Code41240
#define Code47503 nhv_sam_Code47503
#define CopyStress nhv_sam_CopyStress
#define EnableSingmode nhv_sam_EnableSingmode
#define EnableDebug nhv_sam_EnableDebug
#define GetBuffer nhv_sam_GetBuffer
#define GetBufferLength nhv_sam_GetBufferLength
#define GetRuleByte nhv_sam_GetRuleByte
#define Init nhv_sam_Init
#define Insert nhv_sam_Insert
#define InsertBreath nhv_sam_InsertBreath
#define Output8Bit nhv_sam_Output8Bit
#define Output8BitAry nhv_sam_Output8BitAry
#define Parser1 nhv_sam_Parser1
#define Parser2 nhv_sam_Parser2
#define PrepareOutput nhv_sam_PrepareOutput
#define PrintOutput nhv_sam_PrintOutput
#define PrintPhonemes nhv_sam_PrintPhonemes
#define PrintRule nhv_sam_PrintRule
#define Read nhv_sam_Read
#define Render nhv_sam_Render
#define RenderSample nhv_sam_RenderSample
#define SAMMain nhv_sam_SAMMain
#define SetInput nhv_sam_SetInput
#define SetMouth nhv_sam_SetMouth
#define SetMouthThroat nhv_sam_SetMouthThroat
#define SetPhonemeLength nhv_sam_SetPhonemeLength
#define SetPitch nhv_sam_SetPitch
#define SetSpeed nhv_sam_SetSpeed
#define SetThroat nhv_sam_SetThroat
#define TextToPhonemes nhv_sam_TextToPhonemes
#define Write nhv_sam_Write
#define ampl1data nhv_sam_ampl1data
#define ampl2data nhv_sam_ampl2data
#define ampl3data nhv_sam_ampl3data
#define amplitude1 nhv_sam_amplitude1
#define amplitude2 nhv_sam_amplitude2
#define amplitude3 nhv_sam_amplitude3
#define amplitudeRescale nhv_sam_amplitudeRescale
#define blendRank nhv_sam_blendRank
#define buffer nhv_sam_buffer
#define bufferpos nhv_sam_bufferpos
#define debug nhv_sam_debug
#define flags nhv_sam_flags
#define flags2 nhv_sam_flags2
#define freq1data nhv_sam_freq1data
#define freq2data nhv_sam_freq2data
#define freq3data nhv_sam_freq3data
#define frequency1 nhv_sam_frequency1
#define frequency2 nhv_sam_frequency2
#define frequency3 nhv_sam_frequency3
#define inBlendLength nhv_sam_inBlendLength
#define input nhv_sam_input
#define mem39 nhv_sam_mem39
#define mem44 nhv_sam_mem44
#define mem47 nhv_sam_mem47
#define mem49 nhv_sam_mem49
#define mem50 nhv_sam_mem50
#define mem51 nhv_sam_mem51
#define mem53 nhv_sam_mem53
#define mem56 nhv_sam_mem56
#define mem59 nhv_sam_mem59
#define mouth nhv_sam_mouth
#define outBlendLength nhv_sam_outBlendLength
#define phonemeIndexOutput nhv_sam_phonemeIndexOutput
#define phonemeLength nhv_sam_phonemeLength
#define phonemeLengthOutput nhv_sam_phonemeLengthOutput
#define phonemeLengthTable nhv_sam_phonemeLengthTable
#define phonemeStressedLengthTable nhv_sam_phonemeStressedLengthTable
#define phonemeindex nhv_sam_phonemeindex
#define pitch nhv_sam_pitch
#define pitches nhv_sam_pitches
#define rectangle nhv_sam_rectangle
#define rules nhv_sam_rules
#define rules2 nhv_sam_rules2
#define sampleTable nhv_sam_sampleTable
#define sampledConsonantFlag nhv_sam_sampledConsonantFlag
#define sampledConsonantFlags nhv_sam_sampledConsonantFlags
#define signInputTable1 nhv_sam_signInputTable1
#define signInputTable2 nhv_sam_signInputTable2
#define singmode nhv_sam_singmode
#define sinus nhv_sam_sinus
#define speed nhv_sam_speed
#define stress nhv_sam_stress
#define stressInputTable nhv_sam_stressInputTable
#define stressOutput nhv_sam_stressOutput
#define tab36376 nhv_sam_tab36376
#define tab37489 nhv_sam_tab37489
#define tab37515 nhv_sam_tab37515
#define tab47492 nhv_sam_tab47492
#define tab48426 nhv_sam_tab48426
#define throat nhv_sam_throat
#define timetable nhv_sam_timetable
#define trans nhv_sam_trans
#define wait1 nhv_sam_wait1
#define wait2 nhv_sam_wait2

int debug = 0; /* (main.c, which isn't used, defines it) */

/* debug.c's printing only runs with debug on; it isn't built (its table
 * declarations don't match SamTabs.h in one translation unit). (No
 * #include "debug.h" here: that would find the engine's.) */
void PrintPhonemes(unsigned char *a, unsigned char *b, unsigned char *c) { (void)a; (void)b; (void)c; }
void PrintOutput(unsigned char *a, unsigned char *b, unsigned char *c, unsigned char *d, unsigned char *e,
		unsigned char *f, unsigned char *g, unsigned char *h)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}
void PrintRule(int offset) { (void)offset; }

#include "reciter.c"
#include "render.c"
#include "sam.c"

/* Renders one short piece of text (at most ~70 characters: SAM's buffers
 * are 256 bytes once it's spelled out as phonemes) or of SAM phonemes.
 * On success *out is 8-bit unsigned mono PCM at 22050 Hz, malloc'd (the
 * caller frees it), and its length is returned; 0 if SAM can't read it. */
int nhv_sam_render(const char *text, int phonetic, int spd, int ptch, int thrt, int mth, int sing,
		unsigned char **out)
{
	char in[256];
	size_t i, n;
	int len;

	*out = NULL;
	memset(in, 0, sizeof(in));
	n = strlen(text);
	if (n > 200)
		n = 200;
	for (i = 0; i < n; i++)
		in[i] = (char)toupper((unsigned char)text[i]);
	if (phonetic) {
		strcat(in, "\x9b");
	} else {
		strcat(in, "[");
		if (!TextToPhonemes((unsigned char *)in))
			return 0;
	}
	SetSpeed((unsigned char)spd);
	SetPitch((unsigned char)ptch);
	SetThroat((unsigned char)thrt);
	SetMouth((unsigned char)mth);
	singmode = sing ? 1 : 0; /* (EnableSingmode() can't turn it off again) */
	SetInput(in);
	if (!SAMMain()) {
		free(buffer);
		buffer = NULL;
		return 0;
	}
	len = GetBufferLength() / 50;
	if (len <= 0 || len > 22050 * 10) {
		free(buffer);
		buffer = NULL;
		return 0;
	}
	*out = (unsigned char *)malloc((size_t)len);
	if (*out)
		memcpy(*out, buffer, (size_t)len);
	free(buffer); /* (SAM allocates a new one each time) */
	buffer = NULL;
	return *out ? len : 0;
}
