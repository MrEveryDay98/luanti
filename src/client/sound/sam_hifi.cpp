// VoxHack: SAM in hi-fi (see sam_hifi.h).

#include "sam_hifi.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr float RATE = 22050.0f;
constexpr float PI = 3.14159265f;
// one step of SAM's output loop, in samples (its timing: 162/50), and what
// a unit of its formant frequencies is in Hz then (a 256th of a cycle a step)
constexpr float STEP = 162.0f / 50.0f;
constexpr float HZ = RATE / (256.0f * STEP);
// the formants' bandwidths (Hz): how quickly each rings away
const float BANDWIDTH[3] = {90.0f, 110.0f, 170.0f};
// and how loud each is (SAM's balance between them: its third formant is a
// square wave, bright)
const float WEIGHT[3] = {1.0f, 2.2f, 3.0f};
constexpr float ATTACK_S = 0.0012f;  // a pulse's soft start
constexpr float GAIN = 0.9f;
// the voice wavering a little
constexpr float VIBRATO = 0.004f, VIBRATO_HZ = 5.1f;
constexpr float JITTER = 0.006f;     // each pulse's period, a little off
constexpr float SHIMMER = 0.03f;     // each pulse's loudness, a little off
constexpr float BREATH = 0.008f;     // a breath of noise in the voice
// intonation (each a share of the pitch; times the pitch movement):
constexpr float PHRASE_FALL = 0.07f;    // a phrase starts this much higher, ends this much lower
constexpr float COMMA_RISE = 0.12f;     // before a comma or a dash: rising, more to come
constexpr float QUESTION_RISE = 0.25f;  // a question's end
constexpr float STOP_FALL = 0.10f;      // a statement's end, settling
constexpr float ACCENT = 0.11f;         // a stressed syllable's peak (early in it, then falling)
constexpr float ACCENT_PEAK = 0.35f;
constexpr float END_SPAN = 0.3f;        // how much of a phrase its end shapes
constexpr float EXCLAIM_RANGE = 1.6f;   // an exclamation: all of it wider
// whispering: the formants rung by breath, not the voice
constexpr float WHISPER = 1.3f;

// SAM's phonemes that are punctuation (its sign tables: ' ' . ? , -)
enum { PH_PAUSE = 0, PH_STOP = 1, PH_QUESTION = 2, PH_COMMA = 3, PH_DASH = 4 };

// the hissed consonants (SAM's kinds: sam's sampledConsonantFlag & 7, less 1):
// a band of noise each
struct Hiss {
	float centre, q, level;
};
const Hiss HISS[5] = {
	{6000.0f, 1.2f, 0.18f},  // T, S, Z
	{3200.0f, 1.5f, 0.18f},  // CH, J, SH, ZH
	{4500.0f, 0.5f, 0.07f},  // P, F, V, TH, DH
	{1500.0f, 0.6f, 0.06f},  // /H
	{2000.0f, 1.0f, 0.11f},  // /X (the German ch)
};
constexpr float VOICED_HISS = 0.6f;  // Z, ZH, V, DH: the noise under the voice

struct Grain {
	float phase, step, amp, decay;
	int age, attack;
};

struct Bandpass {
	float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	void set(float centre, float q)
	{
		float w = 2 * PI * centre / RATE, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
		b0 = alpha / a0, b2 = -alpha / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - alpha) / a0;
	}
	float run(float x)
	{
		float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1, x1 = x, y2 = y1, y1 = y;
		return y;
	}
};

std::vector<float> g_out;
std::mt19937 g_rng(1);
// the voice's tuning (nhv_sam_hifi_begin)
float g_bright = 1, g_breath = 1, g_pitch = 1, g_hiss = 1;
bool g_whisper = false, g_exclaim = false;

float rnd()
{
	return std::uniform_real_distribution<float>(-1.0f, 1.0f)(g_rng);
}

// the intonation: each frame's pitch, as a share more or less than SAM's -
// phrase by phrase (between punctuation), each settling down from where it
// starts, shaped at its end by what ends it, a peak on each stressed
// syllable
std::vector<float> intonation(int count, int phonemes, const unsigned char *index, const unsigned char *stress,
		const unsigned char *length)
{
	std::vector<float> m(count, 0.0f);
	const float range = g_pitch * (g_exclaim ? EXCLAIM_RANGE : 1.0f);
	if (range <= 0 || phonemes <= 0)
		return std::vector<float>(count, 1.0f);
	// where each phoneme's frames start
	std::vector<int> at(phonemes + 1, 0);
	for (int j = 0; j < phonemes; j++)
		at[j + 1] = std::min(count, at[j] + length[j]);
	auto punct = [&](int j) { return index[j] >= PH_STOP && index[j] <= PH_DASH; };
	int j = 0;
	while (j < phonemes) {
		if (punct(j) || index[j] == PH_PAUSE) {
			j++;
			continue;
		}
		// a phrase: to the next punctuation (or the breath group's end: a statement)
		int a = j;
		while (j < phonemes && !punct(j))
			j++;
		int end = j < phonemes ? index[j] : PH_STOP;
		int fa = at[a], fb = at[j];
		for (int k = fa; k < fb; k++) {
			float x = fb - fa > 1 ? (float)(k - fa) / (fb - fa - 1) : 1.0f;
			float e = std::max(0.0f, (x - (1 - END_SPAN)) / END_SPAN);
			float v = PHRASE_FALL * (1 - 2 * x);
			if (end == PH_COMMA || end == PH_DASH)
				v += COMMA_RISE * e;
			else if (end == PH_QUESTION)
				v += QUESTION_RISE * e;
			else
				v -= STOP_FALL * e;
			m[k] += range * v;
		}
		for (int s = a; s < j; s++) {
			if (!stress[s])
				continue;
			int sa = at[s], sb = at[s + 1];
			for (int k = sa; k < sb; k++) {
				float u = (k - sa + 0.5f) / std::max(1, sb - sa);
				float shape = u < ACCENT_PEAK ? std::sin(PI / 2 * u / ACCENT_PEAK)
						: std::cos(PI / 2 * (u - ACCENT_PEAK) / (1 - ACCENT_PEAK));
				m[k] += range * ACCENT * shape;
			}
		}
	}
	// (smoothed over a few frames: a voice glides)
	std::vector<float> out(count);
	for (int k = 0; k < count; k++) {
		float sum = 0;
		int n = 0;
		for (int d = -2; d <= 2; d++) {
			if (k + d >= 0 && k + d < count) {
				sum += m[k + d];
				n++;
			}
		}
		out[k] = 1 + sum / n;
	}
	return out;
}

} // namespace

extern "C" void nhv_sam_hifi_begin(const float *tuning)
{
	g_bright = std::clamp(tuning[0], 0.0f, 2.0f);
	g_breath = std::clamp(tuning[1], 0.0f, 3.0f);
	g_pitch = std::clamp(tuning[2], 0.0f, 3.0f);
	g_hiss = std::clamp(tuning[3], 0.0f, 3.0f);
	g_whisper = tuning[4] != 0;
	g_exclaim = tuning[5] != 0;
	g_out.clear();
	g_rng.seed(1); // (the same line sounds the same every time)
}

extern "C" void nhv_sam_hifi_frames(int count, int speed, const unsigned char *flags, const unsigned char *f1,
		const unsigned char *f2, const unsigned char *f3, const unsigned char *a1, const unsigned char *a2,
		const unsigned char *a3, const unsigned char *pitches, int phonemes, const unsigned char *index,
		const unsigned char *stress, const unsigned char *length)
{
	if (count <= 0)
		return;
	count = std::min(count, 256);
	const unsigned char *freq[3] = {f1, f2, f3}, *ampl[3] = {a1, a2, a3};
	const float frame = std::max(1, speed) * STEP;  // samples a frame
	const int total = (int)(count * frame);
	auto lerp = [&](const unsigned char *t, int i, float f) {
		int j = std::min(i + 1, count - 1);
		return t[i] + (t[j] - t[i]) * f;
	};
	const std::vector<float> tune = intonation(count, phonemes, index, stress, length);
	// (brighter: the upper formants louder; duller: quieter)
	const float weight[3] = {WEIGHT[0], WEIGHT[1] * g_bright, WEIGHT[2] * g_bright * g_bright};

	std::vector<Grain> grains;
	Bandpass band[5], whisper[3];
	float level[5] = {0, 0, 0, 0, 0};
	for (int k = 0; k < 5; k++)
		band[k].set(HISS[k].centre, HISS[k].q);
	const float glide = 1.0f - std::exp(-1.0f / (0.003f * RATE)); // (3 ms)
	float next_pulse = 0;
	float wamp[3] = {0, 0, 0};

	for (int s = 0; s < total; s++) {
		float pos = s / frame;
		int i = std::min((int)pos, count - 1);
		float f = pos - i;
		int kind = flags[i] & 7;
		bool hissed = (flags[i] & 248) != 0;  // a hissed consonant, no voice
		float v = 0;
		if (g_whisper) {
			// whispered: breath through the three formants (their filters
			// following them, every 32 samples)
			if ((s & 31) == 0) {
				for (int k = 0; k < 3; k++) {
					float hz = std::clamp(lerp(freq[k], i, f) * HZ, 100.0f, RATE * 0.45f);
					whisper[k].set(hz, hz / BANDWIDTH[k]);
					wamp[k] = hissed ? 0.0f : lerp(ampl[k], i, f) / 15.0f * weight[k];
				}
			}
			float n = rnd();
			for (int k = 0; k < 3; k++)
				v += WHISPER * wamp[k] * whisper[k].run(n);
		} else if (s >= next_pulse) {
			// a glottal pulse: the three formants rung
			float t = s / RATE;
			float period = std::max(8.0f, lerp(pitches, i, f)) * STEP;
			float into = tune[i] + (tune[std::min(i + 1, count - 1)] - tune[i]) * f;
			period /= std::max(0.5f, into);
			period *= 1 + g_pitch * (VIBRATO * std::sin(2 * PI * VIBRATO_HZ * t) + JITTER * rnd());
			next_pulse = s + period;
			if (!hissed) {
				float shimmer = 1 + g_pitch * SHIMMER * rnd();
				for (int k = 0; k < 3; k++) {
					float a = lerp(ampl[k], i, f) / 15.0f * weight[k];
					if (a <= 0.001f)
						continue;
					float hz = std::min(lerp(freq[k], i, f) * HZ, RATE * 0.45f);
					grains.push_back({0.0f, 2 * PI * hz / RATE, a * shimmer,
							std::exp(-PI * BANDWIDTH[k] / RATE), 0, (int)(ATTACK_S * RATE)});
				}
			}
		}
		for (Grain &g : grains) {
			float env = g.amp;
			if (g.age < g.attack)
				env *= 0.5f - 0.5f * std::cos(PI * g.age / g.attack);
			v += env * std::sin(g.phase);
			g.phase += g.step;
			g.amp *= g.decay;
			g.age++;
		}
		grains.erase(std::remove_if(grains.begin(), grains.end(),
				[](const Grain &g) { return g.amp < 1e-4f; }), grains.end());
		// the noise: each kind's band, faded in and out
		float voiced = hissed || g_whisper ? 0.0f : std::min(1.0f, (lerp(a1, i, f) + lerp(a2, i, f)) / 15.0f);
		float n = rnd();
		for (int k = 0; k < 5; k++) {
			float want = 0;
			if (kind >= 1 && kind - 1 == k)
				want = g_hiss * HISS[k].level * (hissed ? 1.0f : VOICED_HISS);
			if (k == 3)
				want += g_breath * BREATH * voiced;
			level[k] += (want - level[k]) * glide;
			if (level[k] > 1e-4f)
				v += level[k] * band[k].run(n) * 2.5f;
		}
		g_out.push_back(v * GAIN);
	}
}

extern "C" int nhv_sam_hifi_take(float **out)
{
	*out = nullptr;
	if (g_out.empty())
		return 0;
	// no DC; the highest hiss rolled off (two one-poles at 6.5 kHz, lower
	// for a duller voice); peaks rounded off rather than clipped
	const float cut = std::clamp(6500.0f * (0.5f + 0.5f * g_bright), 2500.0f, 10000.0f);
	const float a = std::exp(-2 * PI * cut / RATE);
	float x1 = 0, y1 = 0, l1 = 0, l2 = 0;
	for (float &v : g_out) {
		float y = v - x1 + 0.995f * y1;
		x1 = v, y1 = y;
		l1 = (1 - a) * y + a * l1;
		l2 = (1 - a) * l1 + a * l2;
		v = std::tanh(l2);
	}
	*out = (float *)std::malloc(g_out.size() * sizeof(float));
	if (!*out)
		return 0;
	std::memcpy(*out, g_out.data(), g_out.size() * sizeof(float));
	int n = (int)g_out.size();
	g_out.clear();
	return n;
}
