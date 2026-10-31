// VoxHack: demos (see nhv_demo.h)
//
// A demo file: "NHVDEMO2\n", "proto <network protocol version> demo <format>
// <build>\n" (a demo plays in any VoxHack speaking the same protocol whose
// demos are of a format it reads: DEMO_FORMAT), the player's name line;
// then, compressed with zstd (flushed every second, so a crash keeps what
// came before), records in time order, each:
//   u8 type, u32 time (ms since its part began), u32 size, data
// types:
//   1 a message from the server: u16 command, its data
//   2 a frame: f32 x, y, z (BS units), f32 yaw, pitch, u8 camera mode
//   3 a setting the client reads: name, 0, value
//   4 the camera's roll: f32 degrees
//   5 the Plane of Water's bubbles: f32 floor y, u16 n, n * 6 f32
//   6 a cutscene's orbit: u8 on, f32 center x, y, z, radius, height,
//     speed, angle, look y
//   7 your inventory opened (nothing)
//   8 a part begins (nothing): each session recorded is a part, the game's
//     own from its start - a saved game continued in a later session is
//     added to its demo as a part of its own, played as a session of its
//     own (the client starting again)
//   9 the game's own fog: f32 distance, max (core.nhv_fog)
//  10 the light the hero carries: f32 level (core.nhv_hero_light)
//  11 the big map's scroll: f32 pixels (core.nhv_map_scroll)
//  12 the Plane of Water's drift: f32 x, y, z (core.nhv_player_drift)
// (NHVDEMO1: the same, not compressed - the first demos; a demo without
// "demo <format>": format 1)
//
// Beside it, <demo>.info: the game's details as they were last ("key=value"
// lines, from the game: set_info) - what the title's Demos list shows - and
// start=<ms>,<ms>..., when the game began in each part (played back from
// there: what came first, the title screen, the prologue, gone through at
// once, unseen).

#include "nhv_demo.h"
#include "nhv_camera.h"
#include "constants.h"
#include "network/networkpacket.h"
#include "network/networkprotocol.h"
#include "settings.h"
#include "porting.h"
#include "filesys.h"
#include "log.h"
#include "version.h"
#include "util/serialize.h"
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <zstd.h>

// (the client's own, outside the messages: game.cpp, hud.cpp)
void nhv_set_fog(float distance, float max);
void nhv_get_fog(float *distance, float *max);
void nhv_set_hero_light(float level);
float nhv_get_hero_light();
void nhv_set_map_scroll(float px, bool instant);
float nhv_get_map_scroll();

namespace nhv_demo
{

static const char MAGIC[] = "NHVDEMO2", MAGIC_PLAIN[] = "NHVDEMO1";
enum : u8 {
	REC_PACKET = 1, REC_FRAME = 2, REC_SETTING = 3, REC_ROLL = 4, REC_BUBBLES = 5,
	REC_ORBIT = 6, REC_INVENTORY = 7, REC_PART = 8, REC_FOG = 9, REC_LIGHT = 10,
	REC_MAP_SCROLL = 11, REC_DRIFT = 12,
};

// The demos' format, as far as playing them back goes. Bump DEMO_FORMAT -
// and DEMO_OLDEST with it - only when a change makes older demos play
// wrongly: to the engine's messages, to what's recorded, or to what the
// game sends that changes how it plays (its rules, its levels). An update
// that leaves all that alone leaves these alone, and its older demos go on
// playing. (Media a demo needs that isn't in the cache - a new install -
// is the game's own file of that name: media_fallback.)
static const int DEMO_FORMAT = 1, DEMO_OLDEST = 1;

// the settings the client reads that the game sets as it goes
static const char *const SETTINGS[] = {
	"nhv_sky", "nhv_acoustics", "nhv_brightness", "nhv_flat_sprites",
	"nhv_sam_dict", "nhv_soundfont",
};

static void put_f32(std::string &s, f32 v)
{
	u8 b[4];
	writeF32(b, v);
	s.append((const char *)b, 4);
}

static f32 get_f32(const std::string &s, size_t &at)
{
	f32 v = at + 4 <= s.size() ? readF32((const u8 *)s.data() + at) : 0.0f;
	at += 4;
	return v;
}

static std::string value(const Info &info, const std::string &key)
{
	for (const auto &kv : info) {
		if (kv.first == key)
			return kv.second;
	}
	return "";
}

static std::string demo_dir()
{
	return porting::path_user + DIR_DELIM + "demos";
}

static Info read_info(const std::string &demo_path)
{
	Info info;
	std::ifstream f(demo_path + ".info", std::ios::binary);
	std::string line;
	while (std::getline(f, line)) {
		size_t eq = line.find('=');
		if (eq != std::string::npos)
			info.emplace_back(line.substr(0, eq), line.substr(eq + 1));
	}
	return info;
}

// its details, and when the game began in each of its parts
static void write_info(const std::string &demo_path, const Info &info, const std::string &starts)
{
	std::ofstream f(demo_path + ".info", std::ios::binary | std::ios::trunc);
	for (const auto &kv : info) {
		if (kv.first != "start")
			f << kv.first << "=" << kv.second << "\n";
	}
	f << "start=" << starts << "\n";
}

// a demo's head read: false and why if this VoxHack can't play it
// (compressed: NHVDEMO2)
static bool read_head(std::istream &f, std::string *name, bool *compressed, int *format,
		std::string *error)
{
	std::string magic, version;
	std::getline(f, magic);
	std::getline(f, version);
	std::getline(f, *name);
	*format = 1;
	if (magic != MAGIC && magic != MAGIC_PLAIN) {
		*error = "That isn't a VoxHack demo.";
		return false;
	}
	*compressed = magic == MAGIC;
	// (the protocol it was recorded with must be this one's, its format one
	// this reads; the first demos don't say - they're tried)
	if (version.rfind("proto ", 0) == 0) {
		std::istringstream ss(version.substr(6));
		int proto = 0;
		std::string word;
		ss >> proto >> word;
		if (word == "demo")
			ss >> *format;
		if (proto != LATEST_PROTOCOL_VERSION) {
			*error = "That demo was recorded with another version of VoxHack (network protocol "
					+ std::to_string(proto) + "; this one plays "
					+ std::to_string(LATEST_PROTOCOL_VERSION) + ").";
			return false;
		}
		if (*format < DEMO_OLDEST || *format > DEMO_FORMAT) {
			*error = std::string("That demo was recorded with ") + (*format < DEMO_OLDEST ?
					"an older" : "a newer") + " version of VoxHack, whose demos this one can't play.";
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------- the list
// a name from the list: in the demos folder, a demo
static bool valid_name(const std::string &file)
{
	const std::string ext = ".nhvdemo";
	return file.size() > ext.size() && file.find_first_of("/\\:") == std::string::npos
			&& file.find("..") == std::string::npos
			&& file.compare(file.size() - ext.size(), ext.size(), ext) == 0;
}

// (all but this one - the one being recorded)
static std::vector<Entry> list_but(const std::string &skip)
{
	std::vector<Entry> out;
	const std::string dir = demo_dir();
	for (const fs::DirListNode &n : fs::GetDirListing(dir)) {
		const std::string path = dir + DIR_DELIM + n.name;
		if (n.dir || !valid_name(n.name) || path == skip)
			continue;
		Entry e;
		e.file = n.name;
		std::ifstream f(path, std::ios::binary);
		bool z = false;
		e.playable = f.good() && read_head(f, &e.name, &z, &e.format, &e.why);
		f.clear();
		f.seekg(0, std::ios::end);
		e.size = (u64)std::max<std::streamoff>(0, f.tellg());
		// (named as it was recorded: 2026-10-30_14-05-09.nhvdemo)
		int Y, M, D, h, m, s;
		if (sscanf(n.name.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d", &Y, &M, &D, &h, &m, &s) == 6) {
			char buf[32];
			snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", Y, M, D, h, m);
			e.date = buf;
		}
		e.info = read_info(path);
		out.push_back(e);
	}
	// (newest first: the names are their dates)
	std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
		return a.file > b.file;
	});
	return out;
}

// ---------------------------------------------------------------- recording
static std::ofstream rec_file;
static ZSTD_CCtx *rec_z = nullptr;
static std::string rec_last_frame;

// into the file, compressed (mode: go on, flush what's in, or end)
static void z_write(const void *src, size_t len, ZSTD_EndDirective mode)
{
	ZSTD_inBuffer in{src, len, 0};
	char out[1 << 16];
	for (;;) {
		ZSTD_outBuffer o{out, sizeof out, 0};
		size_t left = ZSTD_compressStream2(rec_z, &o, &in, mode);
		rec_file.write(out, o.pos);
		if (ZSTD_isError(left))
			break;
		if (mode == ZSTD_e_continue ? in.pos == in.size : left == 0)
			break;
	}
}
static u64 rec_t0 = 0;
// (the server thread's set_info and the client's recording: rec_path and
// the game's details under rec_info_lock)
static std::mutex rec_info_lock;
static std::string rec_path;
static bool rec_had_info = false;
static u32 rec_game_ms = 0; // (when the game's details first came)
static Info rec_info;       // (the game's details now)
static std::map<std::string, std::string> rec_settings;
static f32 rec_roll = 0.0f;
static std::string rec_bubbles, rec_orbit, rec_fog, rec_light, rec_scroll, rec_drift;

bool recording()
{
	return rec_file.is_open();
}

bool recording_game()
{
	std::lock_guard<std::mutex> lock(rec_info_lock);
	return !rec_path.empty() && rec_had_info;
}

static u64 rec_flushed = 0;

static void write_record(u8 type, const std::string &data)
{
	u8 head[9];
	head[0] = type;
	writeU32(head + 1, (u32)(porting::getTimeMs() - rec_t0));
	writeU32(head + 5, (u32)data.size());
	z_write(head, 9, ZSTD_e_continue);
	z_write(data.data(), data.size(), ZSTD_e_continue);
	// (to the disk every second: a game that crashes keeps its demo)
	if (porting::getTimeMs() - rec_flushed > 1000) {
		rec_flushed = porting::getTimeMs();
		z_write(nullptr, 0, ZSTD_e_flush);
		rec_file.flush();
	}
}

void record_begin(const std::string &name)
{
	if (recording() || !play_path.empty() || !(g_settings->exists("nhv_demo_record") && g_settings->getBool("nhv_demo_record")))
		return;
	std::string dir = demo_dir();
	fs::CreateAllDirs(dir);
	char stamp[32];
	time_t now = time(nullptr);
	strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", localtime(&now));
	std::string path = dir + DIR_DELIM + stamp + ".nhvdemo";
	rec_file.open(path, std::ios::binary);
	if (!rec_file.good()) {
		errorstream << "[nhv] demo: can't write " << path << std::endl;
		rec_file.close();
		return;
	}
	// (the network protocol's version and the demos' format: what playing it
	// back depends on - and after them, for the curious, the build it was
	// made with)
	rec_file << MAGIC << "\n" << "proto " << LATEST_PROTOCOL_VERSION << " demo " << DEMO_FORMAT
			<< " " << g_version_hash << "\n" << name << "\n";
	rec_z = ZSTD_createCCtx();
	ZSTD_CCtx_setParameter(rec_z, ZSTD_c_compressionLevel, 6);
	rec_last_frame.clear();
	rec_t0 = porting::getTimeMs();
	{
		std::lock_guard<std::mutex> lock(rec_info_lock);
		rec_path = path;
		rec_had_info = false;
		rec_info.clear();
	}
	rec_settings.clear();
	rec_roll = 0.0f;
	rec_bubbles.clear();
	rec_orbit.clear();
	rec_fog.clear();
	rec_light.clear();
	rec_scroll.clear();
	rec_drift.clear();
	write_record(REC_PART, "");
	actionstream << "[nhv] demo: recording to " << path << std::endl;
}

void record_packet(const NetworkPacket &pkt)
{
	if (!recording())
		return;
	std::string data;
	u8 cmd[2];
	writeU16(cmd, pkt.getCommand());
	data.append((const char *)cmd, 2);
	data.append(pkt.getRemainingNoCopy());
	write_record(REC_PACKET, data);
}

void record_formspec_closed(const std::string &formname)
{
	if (!recording())
		return;
	// (as the server closing it would: an empty formspec of that name)
	NetworkPacket pkt(TOCLIENT_SHOW_FORMSPEC, 0);
	pkt.putLongString("");
	pkt << formname;
	pkt.seek(0); // (read from the start: written, it's at the end)
	record_packet(pkt);
}

void record_inventory_opened()
{
	if (recording())
		write_record(REC_INVENTORY, "");
}

// a record of this type, when what it says changed since the last
static void record_changed(u8 type, std::string *last, const std::string &now)
{
	if (now != *last) {
		*last = now;
		write_record(type, now);
	}
}

// (the camera at most this often: a fast screen draws hundreds of frames a
// second - playback glides between them, play_frame)
static const u64 FRAME_MS = 16;
static u64 rec_frame_ms = 0;

void record_frame(v3f pos, f32 yaw, f32 pitch, u8 camera_mode)
{
	if (!recording())
		return;
	std::string f;
	put_f32(f, pos.X);
	put_f32(f, pos.Y);
	put_f32(f, pos.Z);
	put_f32(f, yaw);
	put_f32(f, pitch);
	f.push_back((char)camera_mode);
	// (standing still, looking the same way: nothing new; changed sooner
	// than FRAME_MS after the last: kept for a frame or two later - the
	// camera where it came to rest is always recorded)
	u64 now = porting::getTimeMs();
	if (f != rec_last_frame && now - rec_frame_ms >= FRAME_MS) {
		rec_frame_ms = now;
		record_changed(REC_FRAME, &rec_last_frame, f);
	}

	// what's passed outside the messages, when it changes
	for (const char *key : SETTINGS) {
		std::string v = g_settings->exists(key) ? g_settings->get(key) : "";
		auto it = rec_settings.find(key);
		if (it == rec_settings.end() || it->second != v) {
			rec_settings[key] = v;
			write_record(REC_SETTING, std::string(key) + '\0' + v);
		}
	}
	f32 roll = nhv_camera::get_roll();
	if (roll != rec_roll) {
		rec_roll = roll;
		std::string r;
		put_f32(r, roll);
		write_record(REC_ROLL, r);
	}
	std::vector<nhv_camera::Bubble> list;
	f32 floor_y = 0.0f;
	u64 at_ms = 0;
	nhv_camera::get_bubbles(&list, &floor_y, &at_ms);
	std::string b;
	put_f32(b, floor_y);
	u8 n[2];
	writeU16(n, (u16)list.size());
	b.append((const char *)n, 2);
	for (const auto &bb : list) {
		for (f32 v : {bb.x, bb.z, bb.rx, bb.rz, bb.vx, bb.vz})
			put_f32(b, v);
	}
	record_changed(REC_BUBBLES, &rec_bubbles, b);
	nhv_camera::Orbit o = nhv_camera::get_orbit();
	std::string ob(1, o.on ? 1 : 0);
	for (f32 v : {o.center.X, o.center.Y, o.center.Z, o.radius, o.height, o.speed, o.angle, o.look_y})
		put_f32(ob, v);
	record_changed(REC_ORBIT, &rec_orbit, ob);
	float fog_distance, fog_max;
	nhv_get_fog(&fog_distance, &fog_max);
	std::string fg;
	put_f32(fg, fog_distance);
	put_f32(fg, fog_max);
	record_changed(REC_FOG, &rec_fog, fg);
	std::string li;
	put_f32(li, nhv_get_hero_light());
	record_changed(REC_LIGHT, &rec_light, li);
	std::string sc;
	put_f32(sc, nhv_get_map_scroll());
	record_changed(REC_MAP_SCROLL, &rec_scroll, sc);
	v3f drift = nhv_camera::get_drift();
	std::string dr;
	put_f32(dr, drift.X);
	put_f32(dr, drift.Y);
	put_f32(dr, drift.Z);
	record_changed(REC_DRIFT, &rec_drift, dr);
}

// (rec_info_lock held, the recording closed) the game in it continued from
// an earlier session's demo - the same game, NetHack's id - that this can
// add to: added to that one as a part of its own instead of kept by itself
static void add_to_earlier()
{
	const std::string game = value(rec_info, "game");
	if (game.empty())
		return;
	for (const Entry &e : list_but(rec_path)) { // (newest first)
		if (!e.playable || e.format != DEMO_FORMAT || value(e.info, "game") != game
				|| value(e.info, "name") != value(rec_info, "name"))
			continue;
		const std::string into = demo_dir() + DIR_DELIM + e.file;
		{
			// (its records, after its head: a zstd stream of its own, read
			// on from the earlier one's)
			std::ifstream src(rec_path, std::ios::binary);
			std::string line;
			for (int i = 0; i < 3; i++)
				std::getline(src, line);
			std::ofstream dst(into, std::ios::binary | std::ios::app);
			dst << src.rdbuf();
			if (!dst.good()) {
				errorstream << "[nhv] demo: can't add to " << into << " (kept by itself)" << std::endl;
				return;
			}
		}
		std::string starts = value(e.info, "start");
		write_info(into, rec_info, (starts.empty() ? "0" : starts) + "," + std::to_string(rec_game_ms));
		fs::DeleteSingleFileOrEmptyDirectory(rec_path + ".info");
		fs::DeleteSingleFileOrEmptyDirectory(rec_path);
		actionstream << "[nhv] demo: the game continued - added to " << into << std::endl;
		return;
	}
}

void record_end()
{
	if (recording()) {
		z_write(nullptr, 0, ZSTD_e_end);
		ZSTD_freeCCtx(rec_z);
		rec_z = nullptr;
		rec_file.close();
		std::lock_guard<std::mutex> lock(rec_info_lock);
		// (no game in it - the title screen, the prologue: not kept)
		if (!rec_had_info) {
			fs::DeleteSingleFileOrEmptyDirectory(rec_path);
			actionstream << "[nhv] demo: recording ended, no game in it (not kept)" << std::endl;
		} else {
			actionstream << "[nhv] demo: recording ended" << std::endl;
			add_to_earlier();
		}
		rec_path.clear();
	}
}

void set_info(const Info &info)
{
	std::lock_guard<std::mutex> lock(rec_info_lock);
	if (rec_path.empty() || info.empty())
		return;
	if (!rec_had_info)
		rec_game_ms = (u32)(porting::getTimeMs() - rec_t0);
	rec_info = info;
	write_info(rec_path, info, std::to_string(rec_game_ms));
	rec_had_info = true;
}

// ---------------------------------------------------------------- playback
std::string play_path;

static std::ifstream play_file;
static bool play_is_open = false;
static f64 play_clock = 0.0;      // ms, in this part
// (from where the game began: jumped to once the client's ready - before,
// it's still taking in the media and definitions it needs first)
static f64 play_skip = 0.0;
// (jumped there: going through what came before at once, unseen and unheard)
static bool play_catchup = false;
static bool play_pause = false, play_done = false;
static bool play_part_break = false; // (the next part's next: this one's over)
static bool play_left = false;       // (left from its pause menu: no more parts)
static f32 play_rate = 1.0f;
static std::deque<std::string> play_packets; // u16 command + data, due
static bool play_have_frame = false;
static v3f play_pos;
static f32 play_yaw = 0.0f, play_pitch = 0.0f;
static u8 play_mode = 0;
// (the camera gliding from the frame before - recorded up to 60 a second,
// shown at whatever rate the screen draws: play_frame)
static v3f glide_pos;
static f32 glide_yaw = 0.0f, glide_pitch = 0.0f;
static f64 glide_t0 = 0.0, glide_ms = 0.0;
// the next record, read ahead (its time decides when it's due)
static bool next_ok = false;
static u8 next_type = 0;
static u32 next_t = 0;
static std::string next_data;
// settings as they were before the demo changed them: put back after
static std::map<std::string, std::pair<bool, std::string>> play_saved;
// what's outside the messages, as it was before: put back after
static float was_fog_distance = -1.0f, was_fog_max = 1.0f, was_light = 0.0f, was_scroll = 0.0f;
static v3f was_drift;
static nhv_camera::Orbit play_orbit_was;
static bool play_inventory = false;
static std::vector<f64> play_starts; // (when the game began in each part, ms)
static size_t play_part = 0;

bool playing()
{
	return play_is_open;
}

// reading: straight from the file (NHVDEMO1), or decompressed from it
static bool play_z = false;
static ZSTD_DCtx *play_dz = nullptr;
static std::string play_buf; // decompressed, not read yet (from play_at)
static size_t play_at = 0;
static std::vector<char> play_zin(1 << 16);
static ZSTD_inBuffer play_zib{nullptr, 0, 0};

// more decompressed into play_buf; false at the end
static bool fill()
{
	play_buf.erase(0, play_at);
	play_at = 0;
	char out[1 << 16];
	for (;;) {
		if (play_zib.pos == play_zib.size) {
			play_file.read(play_zin.data(), play_zin.size());
			size_t got = (size_t)play_file.gcount();
			if (!got)
				return false;
			play_zib = ZSTD_inBuffer{play_zin.data(), got, 0};
		}
		ZSTD_outBuffer o{out, sizeof out, 0};
		size_t r = ZSTD_decompressStream(play_dz, &o, &play_zib);
		if (ZSTD_isError(r))
			return false;
		if (o.pos) {
			play_buf.append(out, o.pos);
			return true;
		}
	}
}

static bool read_bytes(char *dst, size_t n)
{
	if (!play_z)
		return n == 0 || (bool)play_file.read(dst, n);
	while (play_buf.size() - play_at < n) {
		if (!fill())
			return false;
	}
	memcpy(dst, play_buf.data() + play_at, n);
	play_at += n;
	return true;
}

static void read_next()
{
	u8 head[9];
	next_ok = false;
	if (!read_bytes((char *)head, 9))
		return;
	next_type = head[0];
	next_t = readU32(head + 1);
	u32 size = readU32(head + 5);
	next_data.resize(size);
	if (size && !read_bytes(&next_data[0], size))
		return;
	next_ok = true;
}

// what's outside the messages as the demo starts: none of the game's own
// (the demo says, from its first frame - older demos don't)
static void neutral()
{
	nhv_set_fog(-1.0f, 1.0f);
	nhv_set_hero_light(0.0f);
	nhv_set_map_scroll(0.0f, true);
	nhv_camera::set_drift(v3f());
	nhv_camera::stop_orbit();
	nhv_camera::stop_path();
	nhv_camera::set_roll(0.0f);
	nhv_camera::set_bubbles({}, 0.0f);
}

// a part from its start (its mark read): played from when the game began in
// it, if the demo says (a moment before, the title screen's still up)
static void start_part()
{
	play_clock = 0.0;
	play_skip = play_part < play_starts.size() ? play_starts[play_part] : 0.0;
	play_pause = play_done = play_part_break = play_catchup = false;
	play_packets.clear();
	play_have_frame = false;
	play_inventory = false;
	play_orbit_was = nhv_camera::Orbit();
	neutral();
}

static std::string play_name;

bool play_open(std::string *name, std::string *error)
{
	if (play_is_open) { // (already open: the launcher may ask again)
		*name = play_name;
		return true;
	}
	play_file.clear();
	play_file.open(play_path, std::ios::binary);
	if (!play_file.good()) {
		*error = "Can't open the demo " + play_path;
		return false;
	}
	int format = 1;
	if (!read_head(play_file, name, &play_z, &format, error)) {
		*error += " (" + play_path + ")";
		play_file.close();
		return false;
	}
	if (play_z)
		play_dz = ZSTD_createDCtx();
	play_buf.clear();
	play_at = 0;
	play_zib = ZSTD_inBuffer{nullptr, 0, 0};
	play_is_open = true;
	play_name = *name;
	play_rate = 1.0f;
	play_left = false;
	play_saved.clear();
	nhv_get_fog(&was_fog_distance, &was_fog_max);
	was_light = nhv_get_hero_light();
	was_scroll = nhv_get_map_scroll();
	was_drift = nhv_camera::get_drift();
	play_starts.clear();
	std::stringstream ss(value(read_info(play_path), "start"));
	std::string one;
	while (std::getline(ss, one, ','))
		play_starts.push_back(atof(one.c_str()));
	play_part = 0;
	start_part();
	read_next();
	if (next_ok && next_type == REC_PART) // (the first part's mark)
		read_next();
	actionstream << "[nhv] demo: playing " << play_path << " (" << *name << ")" << std::endl;
	return true;
}

bool play_next_part()
{
	if (!play_is_open || play_left)
		return false;
	// (what's left of this part - after the game let go - passed over)
	while (next_ok && next_type != REC_PART)
		read_next();
	if (!next_ok)
		return false;
	read_next();
	play_part++;
	start_part();
	actionstream << "[nhv] demo: part " << play_part + 1 << std::endl;
	return true;
}

static void set_setting(const std::string &key, const std::string &value)
{
	if (!play_saved.count(key)) {
		bool had = g_settings->exists(key);
		play_saved[key] = {had, had ? g_settings->get(key) : ""};
	}
	g_settings->set(key, value);
}

static void apply_record(u8 type, const std::string &d)
{
	size_t at = 0;
	switch (type) {
	case REC_PACKET:
		// (a menu closed, recorded empty by an early recorder: as the
		// server closing whatever menu is open)
		if (d.size() == 2 && readU16((const u8 *)d.data()) == TOCLIENT_SHOW_FORMSPEC) {
			NetworkPacket pkt(TOCLIENT_SHOW_FORMSPEC, 0);
			pkt.putLongString("");
			pkt << std::string();
			pkt.seek(0);
			std::string fixed = d;
			fixed.append(pkt.getRemainingNoCopy());
			play_packets.push_back(fixed);
			break;
		}
		play_packets.push_back(d);
		break;
	case REC_FRAME:
		// (from where the last one was, over as long as it was ago - a
		// frame or so; a long wait - the camera standing still - none)
		glide_pos = play_pos, glide_yaw = play_yaw, glide_pitch = play_pitch;
		glide_ms = play_have_frame && !play_catchup
				? std::min<f64>(play_clock - glide_t0, 2.0 * FRAME_MS) : 0.0;
		glide_t0 = play_clock;
		play_pos.X = get_f32(d, at);
		play_pos.Y = get_f32(d, at);
		play_pos.Z = get_f32(d, at);
		play_yaw = get_f32(d, at);
		play_pitch = get_f32(d, at);
		play_mode = at < d.size() ? (u8)d[at] : 0;
		play_have_frame = true;
		break;
	case REC_SETTING: {
		size_t z = d.find('\0');
		if (z != std::string::npos)
			set_setting(d.substr(0, z), d.substr(z + 1));
		break;
	}
	case REC_ROLL:
		nhv_camera::set_roll(get_f32(d, at));
		break;
	case REC_BUBBLES: {
		f32 floor_y = get_f32(d, at);
		u16 n = at + 2 <= d.size() ? readU16((const u8 *)d.data() + at) : 0;
		at += 2;
		std::vector<nhv_camera::Bubble> list(n);
		for (auto &b : list) {
			b.x = get_f32(d, at);
			b.z = get_f32(d, at);
			b.rx = get_f32(d, at);
			b.rz = get_f32(d, at);
			b.vx = get_f32(d, at);
			b.vz = get_f32(d, at);
		}
		nhv_camera::set_bubbles(list, floor_y);
		break;
	}
	case REC_ORBIT: {
		nhv_camera::Orbit o;
		o.on = !d.empty() && d[0] != 0;
		at = 1;
		o.center.X = get_f32(d, at);
		o.center.Y = get_f32(d, at);
		o.center.Z = get_f32(d, at);
		o.radius = get_f32(d, at);
		o.height = get_f32(d, at);
		o.speed = get_f32(d, at);
		o.angle = get_f32(d, at);
		o.look_y = get_f32(d, at);
		if (!o.on) {
			nhv_camera::stop_orbit();
		} else if (play_orbit_was.on && o.center == play_orbit_was.center &&
				o.radius == play_orbit_was.radius && o.height == play_orbit_was.height &&
				o.speed == play_orbit_was.speed && o.angle == play_orbit_was.angle) {
			nhv_camera::set_orbit_look(o.look_y); // (still going round: look higher)
		} else {
			nhv_camera::set_orbit(o);
		}
		play_orbit_was = o;
		break;
	}
	case REC_INVENTORY:
		play_inventory = true;
		break;
	case REC_FOG: {
		f32 distance = get_f32(d, at);
		nhv_set_fog(distance, get_f32(d, at));
		break;
	}
	case REC_LIGHT:
		nhv_set_hero_light(get_f32(d, at));
		break;
	case REC_MAP_SCROLL:
		nhv_set_map_scroll(get_f32(d, at), false);
		break;
	case REC_DRIFT: {
		v3f v;
		v.X = get_f32(d, at);
		v.Y = get_f32(d, at);
		v.Z = get_f32(d, at);
		nhv_camera::set_drift(v);
		break;
	}
	default:
		break;
	}
}

bool play_take_inventory()
{
	bool was = play_inventory;
	play_inventory = false;
	return was;
}

void play_step(f32 dtime)
{
	if (!play_is_open || play_done)
		return;
	if (!play_pause)
		play_clock += (f64)dtime * 1000.0 * play_rate;
	while (next_ok) {
		// (the next part's mark: this one's over)
		if (next_type == REC_PART) {
			play_part_break = true;
			break;
		}
		if (next_t > play_clock)
			break;
		apply_record(next_type, next_data);
		read_next();
	}
	if ((!next_ok || play_part_break) && play_packets.empty())
		play_done = true;
	// (all that came before taken in: from here it's seen)
	if (play_catchup && play_packets.empty())
		play_catchup = false;
}

void play_running()
{
	if (play_skip > play_clock) {
		play_clock = play_skip;
		play_catchup = true;
	}
	play_skip = 0.0;
}

bool play_catching_up()
{
	return play_catchup;
}

bool play_next_packet(NetworkPacket *pkt)
{
	if (play_packets.empty())
		return false;
	const std::string &d = play_packets.front();
	pkt->putRawPacket((const u8 *)d.data(), (u32)d.size(), PEER_ID_SERVER);
	play_packets.pop_front();
	return true;
}

bool play_frame(v3f *pos, f32 *yaw, f32 *pitch, u8 *camera_mode)
{
	if (!play_have_frame)
		return false;
	*pos = play_pos;
	*yaw = play_yaw;
	*pitch = play_pitch;
	*camera_mode = play_mode;
	if (glide_ms > 0.0 && play_clock < glide_t0 + glide_ms) {
		f32 k = (f32)((play_clock - glide_t0) / glide_ms);
		*pos = glide_pos + (play_pos - glide_pos) * k;
		// (the shorter way round)
		f32 dyaw = std::fmod(play_yaw - glide_yaw + 540.0f, 360.0f) - 180.0f;
		*yaw = glide_yaw + dyaw * k;
		*pitch = glide_pitch + (play_pitch - glide_pitch) * k;
	}
	return true;
}

void play_toggle_pause()
{
	play_pause = !play_pause;
}

bool play_paused()
{
	return play_pause;
}

void play_speed(f32 factor)
{
	play_rate = std::min(8.0f, std::max(0.25f, play_rate * factor));
}

f32 play_speed_now()
{
	return play_rate;
}

bool play_ended()
{
	return play_done;
}

bool play_part_over()
{
	return play_done && play_part_break;
}

void play_leave()
{
	play_left = true;
}

void play_end()
{
	if (!play_is_open)
		return;
	for (auto &kv : play_saved) {
		if (kv.second.first)
			g_settings->set(kv.first, kv.second.second);
		else
			g_settings->remove(kv.first);
	}
	play_saved.clear();
	neutral();
	nhv_set_fog(was_fog_distance, was_fog_max);
	nhv_set_hero_light(was_light);
	nhv_set_map_scroll(was_scroll, true);
	nhv_camera::set_drift(was_drift);
}

void play_close()
{
	if (!play_is_open)
		return;
	play_end();
	play_file.close();
	if (play_dz) {
		ZSTD_freeDCtx(play_dz);
		play_dz = nullptr;
	}
	play_is_open = false;
	play_path.clear();
}

// ---------------------------------------------------------------- the list
std::vector<Entry> list()
{
	std::string recording_now;
	{
		std::lock_guard<std::mutex> lock(rec_info_lock);
		recording_now = rec_path;
	}
	return list_but(recording_now);
}

bool remove(const std::string &file)
{
	if (!valid_name(file))
		return false;
	const std::string path = demo_dir() + DIR_DELIM + file;
	fs::DeleteSingleFileOrEmptyDirectory(path + ".info");
	return fs::DeleteSingleFileOrEmptyDirectory(path);
}

static std::mutex queue_lock;
static std::string queued;
static bool restart_asked = false;

bool queue(const std::string &file)
{
	const std::string path = demo_dir() + DIR_DELIM + file;
	if (!valid_name(file) || !fs::PathExists(path))
		return false;
	std::lock_guard<std::mutex> lock(queue_lock);
	queued = path;
	return true;
}

bool take_queued(std::string *path)
{
	std::lock_guard<std::mutex> lock(queue_lock);
	if (queued.empty())
		return false;
	*path = queued;
	queued.clear();
	return true;
}

void ask_restart()
{
	std::lock_guard<std::mutex> lock(queue_lock);
	restart_asked = true;
}

bool take_restart()
{
	std::lock_guard<std::mutex> lock(queue_lock);
	bool was = restart_asked;
	restart_asked = false;
	return was;
}

// ---------------------------------------------------------------- media
// the game's own file of that name (its mods' textures, sounds, models,
// media, fonts) - for a demo's media missing from the cache; "" if there's none
std::string media_fallback(const std::string &name)
{
	static std::unordered_map<std::string, std::string> index;
	static bool indexed = false;
	if (!indexed) {
		indexed = true;
		std::vector<std::string> files;
		fs::GetRecursiveSubPaths(porting::path_share + DIR_DELIM + "games" + DIR_DELIM
				+ "nethackvoxel" + DIR_DELIM + "mods", files, true, ".");
		for (const std::string &p : files) {
			for (const char *kind : {"textures", "sounds", "models", "media", "fonts"}) {
				if (p.find(std::string(DIR_DELIM) + kind + DIR_DELIM) != std::string::npos) {
					index.emplace(fs::GetFilenameFromPath(p.c_str()), p);
					break;
				}
			}
		}
	}
	auto it = index.find(name);
	return it == index.end() ? "" : it->second;
}

} // namespace nhv_demo
