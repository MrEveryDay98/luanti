// VoxHack: demos (see nhv_demo.h)
//
// A demo file: "NHVDEMO2\n", "proto <network protocol version> <build>\n" (a
// demo plays in any VoxHack speaking the same protocol), the player's name
// line; then, compressed with zstd as one stream (flushed every second, so
// a crash keeps what came before), records in time order, each:
//   u8 type, u32 time (ms since the session started), u32 size, data
// types:
//   1 a message from the server: u16 command, its data
//   2 a frame: f32 x, y, z (BS units), f32 yaw, pitch, u8 camera mode
//   3 a setting the client reads: name, 0, value
//   4 the camera's roll: f32 degrees
//   5 the Plane of Water's bubbles: f32 floor y, u16 n, n * 6 f32
//   6 a cutscene's orbit: u8 on, f32 center x, y, z, radius, height,
//     speed, angle, look y
//   7 your inventory opened (nothing)
// (NHVDEMO1: the same, not compressed - the first demos)
//
// Beside it, <demo>.info: the game's details as they were last ("key=value"
// lines, from the game: set_info) - what the title's Demos list shows - and
// start=<ms>, when the game began (played back from a little before it:
// what came first, the title screen, the prologue, gone through at once).

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
#include <vector>
#include <zstd.h>

namespace nhv_demo
{

static const char MAGIC[] = "NHVDEMO2", MAGIC_PLAIN[] = "NHVDEMO1";
enum : u8 {
	REC_PACKET = 1, REC_FRAME = 2, REC_SETTING = 3, REC_ROLL = 4, REC_BUBBLES = 5,
	REC_ORBIT = 6, REC_INVENTORY = 7,
};

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
// rec_had_info under rec_info_lock)
static std::mutex rec_info_lock;
static std::string rec_path;
static bool rec_had_info = false;
static u32 rec_game_ms = 0; // (when the game's details first came)
static std::map<std::string, std::string> rec_settings;
static f32 rec_roll = 0.0f;
static std::string rec_bubbles, rec_orbit;

bool recording()
{
	return rec_file.is_open();
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
	std::string dir = porting::path_user + DIR_DELIM + "demos";
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
	// (the network protocol's version: what playing it back depends on -
	// and after it, for the curious, the build it was made with)
	rec_file << MAGIC << "\n" << "proto " << LATEST_PROTOCOL_VERSION << " " << g_version_hash
			<< "\n" << name << "\n";
	rec_z = ZSTD_createCCtx();
	ZSTD_CCtx_setParameter(rec_z, ZSTD_c_compressionLevel, 6);
	rec_last_frame.clear();
	rec_t0 = porting::getTimeMs();
	{
		std::lock_guard<std::mutex> lock(rec_info_lock);
		rec_path = path;
		rec_had_info = false;
	}
	rec_settings.clear();
	rec_roll = 0.0f;
	rec_bubbles.clear();
	rec_orbit.clear();
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
	// (standing still, looking the same way: nothing new)
	if (f != rec_last_frame) {
		rec_last_frame = f;
		write_record(REC_FRAME, f);
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
	if (b != rec_bubbles) {
		rec_bubbles = b;
		write_record(REC_BUBBLES, b);
	}
	nhv_camera::Orbit o = nhv_camera::get_orbit();
	std::string ob(1, o.on ? 1 : 0);
	for (f32 v : {o.center.X, o.center.Y, o.center.Z, o.radius, o.height, o.speed, o.angle, o.look_y})
		put_f32(ob, v);
	if (ob != rec_orbit) {
		rec_orbit = ob;
		write_record(REC_ORBIT, ob);
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
	std::ofstream f(rec_path + ".info", std::ios::binary | std::ios::trunc);
	for (const auto &kv : info)
		f << kv.first << "=" << kv.second << "\n";
	f << "start=" << rec_game_ms << "\n";
	rec_had_info = true;
}

// ---------------------------------------------------------------- playback
std::string play_path;

static std::ifstream play_file;
static bool play_is_open = false;
static f64 play_clock = 0.0;      // ms
// (from where the game began: jumped to once the client's ready - before,
// it's still taking in the media and definitions it needs first)
static f64 play_skip = 0.0;
static bool play_pause = false, play_done = false;
static f32 play_rate = 1.0f;
static std::deque<std::string> play_packets; // u16 command + data, due
static bool play_have_frame = false;
static v3f play_pos;
static f32 play_yaw = 0.0f, play_pitch = 0.0f;
static u8 play_mode = 0;
// the next record, read ahead (its time decides when it's due)
static bool next_ok = false;
static u8 next_type = 0;
static u32 next_t = 0;
static std::string next_data;
// settings as they were before the demo changed them: put back after
static std::map<std::string, std::pair<bool, std::string>> play_saved;
static nhv_camera::Orbit play_orbit_was;
static bool play_inventory = false;

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

static std::string play_name;

// a demo's head read: false and why if this VoxHack can't play it
// (compressed: NHVDEMO2)
static bool read_head(std::istream &f, std::string *name, bool *compressed, std::string *error)
{
	std::string magic, version;
	std::getline(f, magic);
	std::getline(f, version);
	std::getline(f, *name);
	if (magic != MAGIC && magic != MAGIC_PLAIN) {
		*error = "That isn't a VoxHack demo.";
		return false;
	}
	*compressed = magic == MAGIC;
	// (the protocol it was recorded with must be this one's; the first demos
	// don't say - they're tried)
	if (version.rfind("proto ", 0) == 0) {
		int proto = atoi(version.c_str() + 6);
		if (proto != LATEST_PROTOCOL_VERSION) {
			*error = "That demo was recorded with another version of VoxHack (network protocol "
					+ std::to_string(proto) + "; this one plays "
					+ std::to_string(LATEST_PROTOCOL_VERSION) + ").";
			return false;
		}
	}
	return true;
}

bool play_open(std::string *name, std::string *error)
{
	if (play_is_open) { // (already open: the launcher may ask again)
		*name = play_name;
		return true;
	}
	play_file.open(play_path, std::ios::binary);
	if (!play_file.good()) {
		*error = "Can't open the demo " + play_path;
		return false;
	}
	if (!read_head(play_file, name, &play_z, error)) {
		*error += " (" + play_path + ")";
		play_file.close();
		return false;
	}
	if (play_z)
		play_dz = ZSTD_createDCtx();
	play_is_open = true;
	play_name = *name;
	play_clock = 0.0;
	play_skip = 0.0;
	// (from a little before the game began, if it says when: what came
	// before it goes by at once)
	std::ifstream inf(play_path + ".info", std::ios::binary);
	std::string line;
	while (std::getline(inf, line)) {
		if (line.rfind("start=", 0) == 0)
			play_skip = std::max(0.0, atof(line.c_str() + 6) - 1500.0);
	}
	read_next();
	actionstream << "[nhv] demo: playing " << play_path << " (" << *name << ")" << std::endl;
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
	while (next_ok && next_t <= play_clock) {
		apply_record(next_type, next_data);
		read_next();
	}
	if (!next_ok && play_packets.empty())
		play_done = true;
}

void play_running()
{
	if (play_skip > play_clock)
		play_clock = play_skip;
	play_skip = 0.0;
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
	nhv_camera::stop_orbit();
	nhv_camera::set_roll(0.0f);
	nhv_camera::set_bubbles({}, 0.0f);
	play_file.close();
	if (play_dz) {
		ZSTD_freeDCtx(play_dz);
		play_dz = nullptr;
	}
	play_is_open = false;
}

// ---------------------------------------------------------------- the list
static std::string demo_dir()
{
	return porting::path_user + DIR_DELIM + "demos";
}

// a name from the list: in the demos folder, a demo
static bool valid_name(const std::string &file)
{
	const std::string ext = ".nhvdemo";
	return file.size() > ext.size() && file.find_first_of("/\\:") == std::string::npos
			&& file.find("..") == std::string::npos
			&& file.compare(file.size() - ext.size(), ext.size(), ext) == 0;
}

std::vector<Entry> list()
{
	std::string recording_now;
	{
		std::lock_guard<std::mutex> lock(rec_info_lock);
		recording_now = rec_path;
	}
	std::vector<Entry> out;
	const std::string dir = demo_dir();
	for (const fs::DirListNode &n : fs::GetDirListing(dir)) {
		const std::string path = dir + DIR_DELIM + n.name;
		if (n.dir || !valid_name(n.name) || path == recording_now)
			continue;
		Entry e;
		e.file = n.name;
		std::ifstream f(path, std::ios::binary);
		bool z = false;
		e.playable = f.good() && read_head(f, &e.name, &z, &e.why);
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
		std::ifstream inf(path + ".info", std::ios::binary);
		std::string line;
		while (std::getline(inf, line)) {
			size_t eq = line.find('=');
			if (eq != std::string::npos)
				e.info.emplace_back(line.substr(0, eq), line.substr(eq + 1));
		}
		out.push_back(e);
	}
	// (newest first: the names are their dates)
	std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
		return a.file > b.file;
	});
	return out;
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

} // namespace nhv_demo
