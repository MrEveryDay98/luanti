// VoxHack: demos - a game session recorded and played back, Quake style.
//
// Recording (setting nhv_demo_record): everything the client receives from
// the server - the level's blocks, the monsters, sounds, the HUD, menus -
// is written to demos/<date>.nhvdemo as it arrives, with the time; and
// every frame where you are and which way you look, what the game passes
// the engine outside the messages (the sky, acoustics, the camera's roll,
// the Plane of Water's bubbles, a cutscene's orbit: nhv_camera.h, and the
// nhv_ settings the client reads), and a menu you closed yourself.
//
// Playing back (luanti --go --nhv-demo <file>): no server and no game - the
// client is fed the recorded messages at the times they came (anything it
// sends goes nowhere), you're put where you were, looking where you
// looked. Space pauses, W and S speed it up and slow it down. A demo plays
// in any VoxHack speaking the same network protocol (the same messages),
// with the media it used in its cache.

#pragma once

#include "irrlichttypes_bloated.h"
#include <string>
#include <utility>
#include <vector>

class NetworkPacket;

namespace nhv_demo
{

// ---------------------------------------------------------------- recording
// start recording a session as player `name` (if nhv_demo_record is on)
void record_begin(const std::string &name);
bool recording();
// a message from the server, before it's handled
void record_packet(const NetworkPacket &pkt);
// a menu (formspec) you closed yourself: played back as the server closing it
void record_formspec_closed(const std::string &formname);
// your inventory opened (the client opens it itself: no message says so)
void record_inventory_opened();
// this frame: where you are (BS units), which way you look, the camera mode
// - and the state passed outside the messages, when it changed
void record_frame(v3f pos, f32 yaw, f32 pitch, u8 camera_mode);
void record_end();

// ---------------------------------------------------------------- playback
// the demo to play (from the command line), "" for none
extern std::string play_path;
bool playing();
// open it: false and why if it can't be played; the recorded player's name
bool play_open(std::string *name, std::string *error);
// the demo's clock goes on (dtime: real seconds; paused or sped up here);
// what's due is applied - settings, shared state - or queued
void play_step(f32 dtime);
// the game's running (the client's ready): from where the game began, if
// the demo says (what came before - the title, the prologue - at once)
void play_running();
// the next message due, if any
bool play_next_packet(NetworkPacket *pkt);
// the inventory was opened now: true once (then it's shown)
bool play_take_inventory();
// where you were and which way you looked now (false: not yet known)
bool play_frame(v3f *pos, f32 *yaw, f32 *pitch, u8 *camera_mode);
// controls
void play_toggle_pause();
bool play_paused();
void play_speed(f32 factor); // times the speed by this (0.25 .. 8)
f32 play_speed_now();
bool play_ended();
void play_end(); // put back the settings it changed

// ---------------------------------------------------------------- the list
// (the title's Demos: core.nhv_demo_*, l_nethack.cpp)
typedef std::vector<std::pair<std::string, std::string>> Info;
// the game's details now, from the server as they change (name, role,
// dlvl...): kept beside the demo being recorded, as <demo>.info ("key=value"
// lines). A recording that never had any - only the title screen or the
// prologue, no game - is thrown away as it ends.
void set_info(const Info &info);
struct Entry {
	std::string file;   // its name in the demos folder
	u64 size = 0;       // bytes
	std::string date;   // when it was recorded, from its name: "2026-10-30 14:05"
	std::string name;   // who recorded it
	bool playable = false;
	std::string why;    // why not
	Info info;          // its .info (none: an older demo)
};
// newest first (not the one being recorded now)
std::vector<Entry> list();
// a demo and its .info deleted (file: a name from list())
bool remove(const std::string &file);
// played when this session ends (the title has the server let you go),
// then back to the game - clientlauncher.cpp
bool queue(const std::string &file);
bool take_queued(std::string *path);

} // namespace nhv_demo
