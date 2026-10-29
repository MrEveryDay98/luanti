// VoxHack: demos (see nhv_demo.h)
//
// A demo file: "NHVDEMO1\n", the engine's version line, the player's name
// line; then records, in time order, each:
//   u8 type, u32 time (ms since the session started), u32 size, data
// types:
//   1 a message from the server: u16 command, its data
//   2 a frame: f32 x, y, z (BS units), f32 yaw, pitch, u8 camera mode
//   3 a setting the client reads: name, 0, value
//   4 the camera's roll: f32 degrees
//   5 the Plane of Water's bubbles: f32 floor y, u16 n, n * 6 f32
//   6 a cutscene's orbit: u8 on, f32 center x, y, z, radius, height,
//     speed, angle, look y

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
#include <map>
#include <vector>

namespace nhv_demo
{

static const char MAGIC[] = "NHVDEMO1";
enum : u8 {
	REC_PACKET = 1, REC_FRAME = 2, REC_SETTING = 3, REC_ROLL = 4, REC_BUBBLES = 5,
	REC_ORBIT = 6,
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
static u64 rec_t0 = 0;
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
	rec_file.write((const char *)head, 9);
	rec_file.write(data.data(), data.size());
	// (to the disk every second: a game that crashes keeps its demo)
	if (porting::getTimeMs() - rec_flushed > 1000) {
		rec_flushed = porting::getTimeMs();
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
	rec_file << MAGIC << "\n" << g_version_hash << "\n" << name << "\n";
	rec_t0 = porting::getTimeMs();
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
	write_record(REC_FRAME, f);

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
		rec_file.close();
		actionstream << "[nhv] demo: recording ended" << std::endl;
	}
}

// ---------------------------------------------------------------- playback
std::string play_path;

static std::ifstream play_file;
static bool play_is_open = false;
static f64 play_clock = 0.0;      // ms
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

bool playing()
{
	return play_is_open;
}

static void read_next()
{
	u8 head[9];
	next_ok = false;
	if (!play_file.read((char *)head, 9))
		return;
	next_type = head[0];
	next_t = readU32(head + 1);
	u32 size = readU32(head + 5);
	next_data.resize(size);
	if (size && !play_file.read(&next_data[0], size))
		return;
	next_ok = true;
}

static std::string play_name;

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
	std::string magic, version;
	std::getline(play_file, magic);
	std::getline(play_file, version);
	std::getline(play_file, *name);
	if (magic != MAGIC) {
		*error = "That isn't a VoxHack demo: " + play_path;
		return false;
	}
	if (version != g_version_hash) {
		*error = "That demo was recorded with another version of VoxHack (" + version
				+ "); this is " + g_version_hash + ".";
		return false;
	}
	play_is_open = true;
	play_name = *name;
	play_clock = 0.0;
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
	default:
		break;
	}
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
	play_is_open = false;
}

} // namespace nhv_demo
