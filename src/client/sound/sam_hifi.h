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
//   - the pitch drifts down over a sentence and wavers a little, as a
//     voice does
//   - no DC, and the highest hiss rolled off
// So it's still SAM's words, timing and voice, without the 8-bit edge.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// (sam_unity.c) start a line: what's voiced next goes into a new buffer.
// The voice's tuning (each 1 as made; 0..2): brightness (the upper
// formants, the highs kept), breath (the noise under the voice), pitch
// movement (its drift and waver; 0: flat), hiss (the hissed consonants)
void nhv_sam_hifi_begin(float bright, float breath, float pitch, float hiss);
// (sam_unity.c) SAM's frames for one breath group, voiced and added on
void nhv_sam_hifi_frames(int count, int speed, const unsigned char *flags, const unsigned char *f1,
		const unsigned char *f2, const unsigned char *f3, const unsigned char *a1, const unsigned char *a2,
		const unsigned char *a3, const unsigned char *pitches);
// what was voiced since begin: mono float samples at 22050 Hz (about -1..1);
// *out is malloc'd (the caller frees it); returns the count
int nhv_sam_hifi_take(float **out);

#ifdef __cplusplus
}
#endif
