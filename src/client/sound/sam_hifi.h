// VoxHack: SAM in hi-fi.
//
// SAM works out its speech as frames (10 ms or so each, at its speed): the
// pitch, three formants' frequencies and loudness, and which frames are
// hissed consonants. Then, as a 1982 program, it plays them through 4-bit
// tables into 8-bit samples, restarting its waves at every glottal pulse -
// the grit and buzz of its voice. Hi-fi takes those same frames (sam_unity.c
// catches them) and voices them anew, in floating point:
//   - each glottal pulse rings the three formants as damped, softly started
//     waves (formant wave functions), overlapping the next pulse's
//   - the frames' values glide from one to the next, not in steps
//   - hissed consonants are filtered noise, shaped like each kind (s, sh,
//     f, h, the German ch), with a breath of it in the voice too
//   - intonation, phrase by phrase: each settles down from where it
//     starts, rising before a comma and at a question's end, falling at
//     a statement's; a peak on each stressed syllable; an exclamation's
//     wider; and the voice wavers a little
//   - or whispered: the formants rung by breath alone
//   - no DC, and the highest hiss rolled off
// So it's still SAM's words, timing and voice, without the 8-bit edge.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// (sam_unity.c) start a line: what's voiced next goes into a new buffer.
// tuning[6]: brightness (the upper formants, the highs kept), breath (the
// noise under the voice), pitch movement (intonation and waver; 0: flat),
// hiss (the hissed consonants) - each 1 as made, 0..2 or 3 - then whisper
// and exclaimed (0 or 1)
void nhv_sam_hifi_begin(const float *tuning);
// (sam_unity.c) SAM's frames for one breath group, voiced and added on;
// with its phonemes (SAM's index, stress and length in frames of each),
// for the intonation
void nhv_sam_hifi_frames(int count, int speed, const unsigned char *flags, const unsigned char *f1,
		const unsigned char *f2, const unsigned char *f3, const unsigned char *a1, const unsigned char *a2,
		const unsigned char *a3, const unsigned char *pitches, int phonemes, const unsigned char *index,
		const unsigned char *stress, const unsigned char *length);
// what was voiced since begin: mono float samples at 22050 Hz (about -1..1);
// *out is malloc'd (the caller frees it); returns the count
int nhv_sam_hifi_take(float **out);

#ifdef __cplusplus
}
#endif
