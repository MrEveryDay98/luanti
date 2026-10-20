// VoxHack: what SAM says and how it sounds (see sam_speech.h).

#include "sam_speech.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
int nhv_sam_text_to_phonemes(const char *text, char *out, int out_size);
int nhv_sam_render_phonemes(const char *phonemes, int spd, int ptch, int thrt, int mth, int sing,
		unsigned char **out);
}

namespace nhvsam {

namespace {

constexpr size_t TEXT_RUN = 70;        // characters SAM can spell at once
constexpr size_t PHONEMES_MAX = 240;   // phonemes SAM can read at once
constexpr size_t PIECE_SOFT = 60;      // a piece ends at the next clause after this
constexpr int FADE = RATE * 4 / 1000;  // soft edges of each piece (4 ms)
constexpr int GAP = RATE * 6 / 100;    // a breath between pieces

std::string lower(std::string s)
{
	for (char &c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

bool hasAlnum(const std::string &s)
{
	return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c); });
}

// curly quotes, dashes and ellipses to plain ASCII; other non-ASCII goes
std::string asciify(const std::string &in)
{
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		unsigned char c = in[i];
		if (c < 0x80) {
			out += (char)c;
			continue;
		}
		if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80) {
			unsigned char d = in[i + 2];
			i += 2;
			if (d == 0x98 || d == 0x99)
				out += '\'';
			else if (d == 0x9C || d == 0x9D)
				out += '"';
			else if (d == 0x93 || d == 0x94)
				out += ", ";
			else if (d == 0xA6)
				out += "...";
			else
				out += ' ';
			continue;
		}
		if ((c == 0xC3) && i + 1 < in.size()) {
			// accented Latin letters (U+00C0-U+00FF) read as their plain letter
			static const char BASE[] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUYTsaaaaaaaceeeeiiiidnooooo ouuuuyty";
			unsigned char d = in[i + 1];
			if (d >= 0x80 && d <= 0xBF) {
				out += BASE[d - 0x80];
				i++;
				continue;
			}
		}
		// skip the rest of this UTF-8 character
		while (i + 1 < in.size() && ((unsigned char)in[i + 1] & 0xC0) == 0x80)
			i++;
		out += ' ';
	}
	return out;
}

// ---------------------------------------------------------------- dictionary
std::string applyDictionary(const std::string &in, const Dictionary &dict)
{
	if (dict.empty())
		return in;
	std::string out;
	size_t i = 0;
	while (i < in.size()) {
		if (in[i] == '{') {  // written phonemes stay as they are
			size_t j = in.find('}', i);
			j = j == std::string::npos ? in.size() : j + 1;
			out += in.substr(i, j - i);
			i = j;
			continue;
		}
		if (!std::isalnum((unsigned char)in[i])) {
			out += in[i++];
			continue;
		}
		size_t j = i;
		while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j] == '\''))
			j++;
		std::string word = in.substr(i, j - i);
		std::string key = lower(word);
		auto it = dict.find(key);
		if (it != dict.end()) {
			out += it->second;
		} else if (key.size() > 2 && key.compare(key.size() - 2, 2, "'s") == 0
				&& (it = dict.find(key.substr(0, key.size() - 2))) != dict.end()) {
			// Croesus's: the word's replacement, then the 's
			const std::string &r = it->second;
			if (!r.empty() && r.back() == '}')
				out += r.substr(0, r.size() - 1) + "Z}";
			else
				out += r + "'s";
		} else {
			out += word;
		}
		i = j;
	}
	return out;
}

// ---------------------------------------------------------------- numbers
const char *ONES[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
	"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
	"nineteen"};
const char *TENS[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

std::string below1000(int n)
{
	std::string s;
	if (n >= 100) {
		s = std::string(ONES[n / 100]) + " hundred";
		n %= 100;
		if (n)
			s += " and ";
	}
	if (n >= 20) {
		s += TENS[n / 10];
		if (n % 10)
			s += std::string("-") + ONES[n % 10];
	} else if (n > 0 || s.empty()) {
		s += ONES[n];
	}
	return s;
}

std::string numberWords(unsigned long long n)
{
	if (n < 1000)
		return below1000((int)n);
	static const char *SCALE[] = {"", " thousand", " million", " billion", " trillion"};
	std::vector<std::string> parts;
	for (int k = 0; n > 0 && k < 5; k++, n /= 1000) {
		int g = (int)(n % 1000);
		if (g)
			parts.push_back(below1000(g) + SCALE[k]);
	}
	std::string s;
	for (size_t i = parts.size(); i-- > 0;)
		s += (s.empty() ? "" : " ") + parts[i];
	return s;
}

std::string ordinal(std::string w)
{
	static const std::pair<const char *, const char *> IRREGULAR[] = {{"one", "first"}, {"two", "second"},
		{"three", "third"}, {"five", "fifth"}, {"eight", "eighth"}, {"nine", "ninth"}, {"twelve", "twelfth"}};
	size_t cut = w.find_last_of(" -");
	std::string head = cut == std::string::npos ? "" : w.substr(0, cut + 1);
	std::string last = cut == std::string::npos ? w : w.substr(cut + 1);
	for (auto &p : IRREGULAR)
		if (last == p.first)
			return head + p.second;
	if (!last.empty() && last.back() == 'y')
		return head + last.substr(0, last.size() - 1) + "ieth";
	return head + last + "th";
}

// numbers and symbols as words; anything SAM can't say becomes a space
std::string wordsForSymbols(const std::string &in)
{
	std::string out;
	size_t i = 0;
	auto prevSpace = [&]() { return out.empty() || out.back() == ' ' || out.back() == '('; };
	while (i < in.size()) {
		char c = in[i];
		bool digitNext = i + 1 < in.size() && std::isdigit((unsigned char)in[i + 1]);
		if (std::isdigit((unsigned char)c)) {
			// 2,400 / 1.5 / 21st
			auto digit = [&](size_t k) { return k < in.size() && std::isdigit((unsigned char)in[k]); };
			// a comma between groups of three digits, as in 2,400
			auto groupComma = [&](size_t k) {
				return in[k] == ',' && digit(k + 1) && digit(k + 2) && digit(k + 3) && !digit(k + 4);
			};
			std::string digits;
			size_t j = i;
			while (j < in.size() && (digit(j) || groupComma(j))) {
				if (in[j] != ',')
					digits += in[j];
				j++;
			}
			std::string words = digits.size() > 15 ? "" : numberWords(std::strtoull(digits.c_str(), nullptr, 10));
			if (words.empty())  // too long: digit by digit
				for (char d : digits)
					words += std::string(words.empty() ? "" : " ") + ONES[d - '0'];
			if (j + 1 < in.size() && in[j] == '.' && std::isdigit((unsigned char)in[j + 1])) {
				words += " point";
				for (j++; j < in.size() && std::isdigit((unsigned char)in[j]); j++)
					words += std::string(" ") + ONES[in[j] - '0'];
			}
			std::string suf = j + 1 < in.size() ? lower(in.substr(j, 2)) : "";
			if ((suf == "st" || suf == "nd" || suf == "rd" || suf == "th")
					&& (j + 2 >= in.size() || !std::isalpha((unsigned char)in[j + 2]))) {
				words = ordinal(words);
				j += 2;
			}
			out += words;
			i = j;
			continue;
		}
		if (c == '+' && digitNext) {
			out += prevSpace() ? "plus " : " plus ";
		} else if (c == '-' && digitNext && prevSpace()) {
			out += "minus ";
		} else if (c == '&') {
			out += " and ";
		} else if (c == '+') {
			out += " plus ";
		} else if (c == '=') {
			out += " equals ";
		} else if (c == '@') {
			out += " at ";
		} else if (c == '#') {
			out += " number ";
		} else if (c == '%') {
			out += " percent";
		} else if (std::isalpha((unsigned char)c) || std::strchr(" .,!?'-:;", c)) {
			out += c;
		} else {
			out += ' ';
		}
		i++;
	}
	return out;
}

// ---------------------------------------------------------------- pieces
struct Token {
	bool phon;
	std::string s;
};

// text and {PHONEMES} as tokens (words keep their punctuation)
std::vector<Token> tokenize(const std::string &in)
{
	std::vector<Token> out;
	size_t i = 0;
	std::string text;
	auto flush = [&]() {
		std::string t = wordsForSymbols(text);
		size_t a = 0;
		while (a < t.size()) {
			while (a < t.size() && t[a] == ' ')
				a++;
			size_t b = t.find(' ', a);
			b = b == std::string::npos ? t.size() : b;
			if (b > a)
				out.push_back({false, t.substr(a, b - a)});
			a = b;
		}
		text.clear();
	};
	while (i < in.size()) {
		if (in[i] == '{') {
			flush();
			size_t j = in.find('}', i);
			j = j == std::string::npos ? in.size() : j;
			std::string p = in.substr(i + 1, j - i - 1);
			for (char &c : p)
				c = (char)std::toupper((unsigned char)c);
			if (!p.empty())
				out.push_back({true, p});
			i = j + 1;
		} else {
			text += in[i++];
		}
	}
	flush();
	return out;
}

bool endsSentence(const Token &t)
{
	return !t.phon && !t.s.empty() && std::strchr(".!?", t.s.back());
}

// one piece's phonemes: its words spelled by SAM, its {PHONEMES} as written
bool piecePhonemes(const std::vector<Token> &toks, size_t a, size_t b, std::string &out)
{
	out.clear();
	std::string run;
	auto spell = [&]() -> bool {
		if (run.empty())
			return true;
		bool ok = true;
		if (hasAlnum(run) || std::strpbrk(run.c_str(), ".,!?")) {
			char ph[256];
			if (nhv_sam_text_to_phonemes(run.c_str(), ph, sizeof(ph)))
				out += (out.empty() ? "" : " ") + std::string(ph);
			else
				ok = !hasAlnum(run);
		}
		run.clear();
		return ok;
	};
	for (size_t i = a; i < b; i++) {
		if (toks[i].phon) {
			if (!spell())
				return false;
			out += (out.empty() ? "" : " ") + toks[i].s;
		} else {
			if (run.size() + toks[i].s.size() + 1 > TEXT_RUN && !spell())
				return false;
			run += (run.empty() ? "" : " ") + toks[i].s;
		}
	}
	return spell();
}

struct Piece {
	std::string phonemes;
	bool exclaim;
};

void addPieces(const std::vector<Token> &toks, size_t a, size_t b, std::vector<Piece> &out)
{
	if (a >= b)
		return;
	std::string ph;
	bool ok = piecePhonemes(toks, a, b, ph);
	if (ok && ph.size() <= PHONEMES_MAX) {
		if (ph.find_first_not_of(" ") != std::string::npos)
			out.push_back({ph, endsSentence(toks[b - 1]) && toks[b - 1].s.back() == '!'});
		return;
	}
	if (b - a == 1)
		return;  // one word SAM can't say: left out
	size_t mid = a + (b - a) / 2;
	addPieces(toks, a, mid, out);
	addPieces(toks, mid, b, out);
}

std::vector<Piece> pieces(const std::string &text, const Settings &s, const Dictionary *dict)
{
	std::vector<Piece> out;
	if (s.phonetic) {
		// written phonemes: split only where SAM's buffer needs it
		std::string p, all = text;
		for (char &c : all)
			c = (char)std::toupper((unsigned char)c);
		size_t i = 0;
		while (i < all.size()) {
			size_t n = std::min(PHONEMES_MAX, all.size() - i);
			if (i + n < all.size()) {
				size_t cut = all.rfind(' ', i + n);
				if (cut != std::string::npos && cut > i)
					n = cut - i;
			}
			p = all.substr(i, n);
			if (p.find_first_not_of(" ") != std::string::npos)
				out.push_back({p, false});
			i += n;
		}
		return out;
	}
	std::string t = asciify(text);
	if (dict)
		t = applyDictionary(t, *dict);
	std::vector<Token> toks = tokenize(t);
	size_t start = 0, len = 0;
	for (size_t i = 0; i < toks.size(); i++) {
		len += toks[i].s.size() + 1;
		const std::string &w = toks[i].s;
		bool clause = !toks[i].phon && !w.empty() && std::strchr(",;:", w.back());
		if (endsSentence(toks[i]) || (clause && len > PIECE_SOFT / 2) || len > PIECE_SOFT) {
			addPieces(toks, start, i + 1, out);
			start = i + 1;
			len = 0;
		}
	}
	addPieces(toks, start, toks.size(), out);
	return out;
}

// ---------------------------------------------------------------- effects
// A chain like "echo:delay=250:decay=0.4/reverb:room=60:mix=35", applied in
// order. Every parameter has a default, so "reverb" alone works too. The
// sound editor (samvoice.py EFFECTS) lists the same effects and parameters.
using Params = std::unordered_map<std::string, float>;

float param(const Params &p, const char *key, float def, float lo, float hi)
{
	auto it = p.find(key);
	float v = it == p.end() || !std::isfinite(it->second) ? def : it->second;
	return std::clamp(v, lo, hi);
}

const float PI = 3.14159265f;

// a delay line read with fractional delays (chorus, vibrato)
float tap(const std::vector<float> &x, float pos)
{
	if (pos < 0)
		return 0;
	size_t i = (size_t)pos;
	if (i + 1 >= x.size())
		return i < x.size() ? x[i] : 0;
	float f = pos - i;
	return x[i] * (1 - f) + x[i + 1] * f;
}

void echo(std::vector<float> &x, const Params &p)
{
	size_t d = (size_t)(param(p, "delay", 250, 20, 1500) / 1000 * RATE);
	float decay = param(p, "decay", 0.4f, 0.05f, 0.95f);
	int repeats = (int)param(p, "repeats", 3, 1, 8);
	size_t n = x.size();
	x.resize(n + d * repeats, 0);
	std::vector<float> dry(x.begin(), x.begin() + n);
	float g = 1;
	for (int k = 1; k <= repeats; k++) {
		g *= decay;
		for (size_t i = 0; i < n; i++)
			x[i + d * k] += dry[i] * g;
	}
}

// a small Freeverb-style room: four damped combs, then two all-passes
void reverb(std::vector<float> &x, const Params &p)
{
	float room = param(p, "room", 60, 0, 100) / 100, mix = param(p, "mix", 35, 0, 100) / 100;
	float feedback = 0.7f + 0.27f * room, damp = 0.45f - 0.25f * room;
	size_t tail = (size_t)((0.3f + 2.7f * room) * RATE);
	size_t n = x.size();
	x.resize(n + tail, 0);
	static const int COMB[] = {558, 594, 638, 678}, ALL[] = {278, 220};
	std::vector<float> wet(x.size(), 0);
	for (int c : COMB) {
		std::vector<float> buf(c, 0);
		float low = 0;
		for (size_t i = 0, j = 0; i < x.size(); i++, j = (j + 1) % c) {
			float out = buf[j];
			low = out * (1 - damp) + low * damp;
			buf[j] = x[i] + low * feedback;
			wet[i] += out * 0.25f;
		}
	}
	for (int a : ALL) {
		std::vector<float> buf(a, 0);
		for (size_t i = 0, j = 0; i < wet.size(); i++, j = (j + 1) % a) {
			float b = buf[j];
			float out = -wet[i] + b;
			buf[j] = wet[i] + b * 0.5f;
			wet[i] = out;
		}
	}
	for (size_t i = 0; i < x.size(); i++)
		x[i] = x[i] * (1 - mix * 0.5f) + wet[i] * mix * 1.6f;
}

void chorus(std::vector<float> &x, const Params &p)
{
	float depth = param(p, "depth", 3, 0.5f, 15) / 1000 * RATE, rate = param(p, "rate", 0.8f, 0.05f, 8);
	float mix = param(p, "mix", 50, 0, 100) / 100, base = 0.02f * RATE;
	std::vector<float> dry = x;
	for (size_t i = 0; i < x.size(); i++) {
		float t = (float)i / RATE;
		float a = tap(dry, i - base - depth * (0.5f + 0.5f * std::sin(2 * PI * rate * t)));
		float b = tap(dry, i - base * 1.4f - depth * (0.5f + 0.5f * std::sin(2 * PI * rate * 1.3f * t + 1.7f)));
		x[i] = dry[i] * (1 - mix * 0.5f) + (a + b) * 0.5f * mix;
	}
}

void vibrato(std::vector<float> &x, const Params &p)
{
	float depth = param(p, "depth", 2, 0.2f, 10) / 1000 * RATE, rate = param(p, "rate", 5, 0.5f, 20);
	std::vector<float> dry = x;
	for (size_t i = 0; i < x.size(); i++)
		x[i] = tap(dry, i - depth * (1 + std::sin(2 * PI * rate * i / RATE)));
}

void tremolo(std::vector<float> &x, const Params &p)
{
	float rate = param(p, "rate", 6, 0.5f, 30), depth = param(p, "depth", 50, 0, 100) / 100;
	for (size_t i = 0; i < x.size(); i++)
		x[i] *= 1 - depth * (0.5f + 0.5f * std::sin(2 * PI * rate * i / RATE));
}

void distortion(std::vector<float> &x, const Params &p)
{
	float g = std::pow(10.0f, param(p, "drive", 12, 0, 40) / 20);
	float norm = std::tanh(g);
	for (float &v : x)
		v = std::tanh(v / 32768 * g) / norm * 32768 * 0.8f;
}

void bitcrush(std::vector<float> &x, const Params &p)
{
	float levels = std::pow(2.0f, param(p, "bits", 6, 1, 16) - 1);
	int hold = (int)param(p, "downsample", 2, 1, 16);
	float held = 0;
	for (size_t i = 0; i < x.size(); i++) {
		if (i % hold == 0)
			held = std::round(x[i] / 32768 * levels) / levels * 32768;
		x[i] = held;
	}
}

// ring modulation: SAM's voice times a tone, a buzzing machine
void robot(std::vector<float> &x, const Params &p)
{
	float f = param(p, "freq", 60, 10, 2000), mix = param(p, "mix", 70, 0, 100) / 100;
	for (size_t i = 0; i < x.size(); i++)
		x[i] = x[i] * (1 - mix) + x[i] * std::sin(2 * PI * f * i / RATE) * mix * 1.4f;
}

// RBJ biquad low/high-pass
void biquad(std::vector<float> &x, float freq, bool high)
{
	float w = 2 * PI * std::clamp(freq, 20.0f, RATE * 0.45f) / RATE, alpha = std::sin(w) / (2 * 0.7071f);
	float c = std::cos(w), a0 = 1 + alpha;
	float b0 = (high ? (1 + c) : (1 - c)) / 2, b1 = high ? -(1 + c) : (1 - c), b2 = b0;
	float a1 = -2 * c, a2 = 1 - alpha;
	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	for (float &v : x) {
		float y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
		x2 = x1, x1 = v, y2 = y1, y1 = y;
		v = y;
	}
}

void applyEffects(std::vector<float> &x, const std::string &chain)
{
	size_t i = 0;
	while (i < chain.size()) {
		size_t e = chain.find('/', i);
		e = e == std::string::npos ? chain.size() : e;
		std::string item = chain.substr(i, e - i);
		i = e + 1;
		// "type:key=value:key=value"
		Params p;
		std::string type;
		size_t a = 0;
		while (a <= item.size()) {
			size_t b = item.find(':', a);
			b = b == std::string::npos ? item.size() : b;
			std::string part = item.substr(a, b - a);
			size_t eq = part.find('=');
			if (type.empty() && eq == std::string::npos)
				type = lower(part);
			else if (eq != std::string::npos)
				p[lower(part.substr(0, eq))] = std::strtof(part.c_str() + eq + 1, nullptr);
			a = b + 1;
		}
		if (type == "echo") echo(x, p);
		else if (type == "reverb") reverb(x, p);
		else if (type == "chorus") chorus(x, p);
		else if (type == "vibrato") vibrato(x, p);
		else if (type == "tremolo") tremolo(x, p);
		else if (type == "distortion") distortion(x, p);
		else if (type == "bitcrush") bitcrush(x, p);
		else if (type == "robot") robot(x, p);
		else if (type == "lowpass") biquad(x, param(p, "freq", 3000, 100, 10000), false);
		else if (type == "highpass") biquad(x, param(p, "freq", 300, 20, 5000), true);
		else if (type == "gain") {
			float g = std::pow(10.0f, param(p, "db", 0, -30, 30) / 20);
			for (float &v : x)
				v *= g;
		}
	}
	// whatever the chain did, never clip
	float peak = 0;
	for (float v : x)
		peak = std::max(peak, std::fabs(v));
	if (peak > 32000)
		for (float &v : x)
			v *= 32000 / peak;
	// trim the silent end of echo and reverb tails
	size_t end = x.size();
	while (end > 0 && std::fabs(x[end - 1]) < 40)
		end--;
	x.resize(std::min(x.size(), end + RATE / 20));
}

} // namespace

// ---------------------------------------------------------------- public
Dictionary parseDictionary(const std::string &text)
{
	Dictionary d;
	size_t i = 0;
	while (i < text.size()) {
		size_t e = text.find('\n', i);
		e = e == std::string::npos ? text.size() : e;
		std::string line = text.substr(i, e - i);
		i = e + 1;
		size_t hash = line.find('#');
		if (hash != std::string::npos)
			line = line.substr(0, hash);
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		auto trim = [](std::string s) {
			size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		};
		std::string word = lower(trim(line.substr(0, eq))), say = trim(line.substr(eq + 1));
		if (!word.empty() && !say.empty())
			d[word] = say;
	}
	return d;
}

std::vector<std::string> toPhonemes(const std::string &text, const Settings &s, const Dictionary *dict)
{
	std::vector<std::string> out;
	for (const Piece &p : pieces(text, s, dict))
		out.push_back(p.phonemes);
	return out;
}

std::vector<short> speak(const std::string &text, const Settings &s, const Dictionary *dict)
{
	std::vector<short> pcm;
	for (const Piece &p : pieces(text, s, dict)) {
		int speed = s.speed, pitch = s.pitch;
		if (s.tone && p.exclaim) {
			// said with more force: quicker, and higher (SAM: lower = higher)
			speed = speed * 85 / 100;
			pitch -= 6;
		}
		unsigned char *buf = nullptr;
		int n = nhv_sam_render_phonemes(p.phonemes.c_str(), std::clamp(speed, 1, 255), std::clamp(pitch, 1, 255),
				std::clamp(s.throat, 1, 255), std::clamp(s.mouth, 1, 255), s.sing, &buf);
		if (n <= 0)
			continue;
		if (!pcm.empty())
			pcm.insert(pcm.end(), GAP, 0);
		size_t at = pcm.size();
		for (int i = 0; i < n; i++)
			pcm.push_back((short)(((int)buf[i] - 128) * 256));
		std::free(buf);
		// soft edges, so pieces don't click
		int fade = std::min(FADE, n / 2);
		for (int i = 0; i < fade; i++) {
			float g = (float)i / fade;
			pcm[at + i] = (short)(pcm[at + i] * g);
			pcm[pcm.size() - 1 - i] = (short)(pcm[pcm.size() - 1 - i] * g);
		}
	}
	if (s.smooth && !pcm.empty()) {
		// SAM's 8-bit sound, softened: no DC offset, and the harsh highs
		// (above ~3.5 kHz) rolled off by three one-pole low-passes
		const float a = std::exp(-2.0f * 3.14159265f * 3500.0f / RATE);
		float x1 = 0, y1 = 0, l1 = 0, l2 = 0, l3 = 0;
		for (short &v : pcm) {
			float x = v;
			float y = x - x1 + 0.995f * y1;  // DC blocker
			x1 = x, y1 = y;
			l1 = (1 - a) * y + a * l1;
			l2 = (1 - a) * l1 + a * l2;
			l3 = (1 - a) * l2 + a * l3;
			v = (short)std::clamp(l3, -32768.0f, 32767.0f);
		}
	}
	if (!s.effects.empty() && !pcm.empty()) {
		std::vector<float> x(pcm.begin(), pcm.end());
		applyEffects(x, s.effects);
		pcm.resize(x.size());
		for (size_t i = 0; i < x.size(); i++)
			pcm[i] = (short)std::clamp(x[i], -32768.0f, 32767.0f);
	}
	return pcm;
}

} // namespace nhvsam
