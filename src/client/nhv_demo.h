// VoxHack: demos - a game session recorded and played back, Quake style.
//
// Recording (setting nhv_demo_record): everything the client receives from
// the server - the level's blocks, the monsters, sounds, the HUD, menus -
// is written to demos/<date>.nhvdemo as it arrives, with the time; and
// every frame where you are and which way you look, what the game passes
// the engine outside the messages (the sky, acoustics, the camera's roll,
// the Plane of Water's bubbles and drift, a cutscene's orbit: nhv_camera.h;
// the game's own fog, the hero's light, the big map's scroll; the nhv_
// settings the client reads), and a menu you closed yourself. A saved game
// continued in a later session is added to its demo, as a part of its own.
//
// Playing back (luanti --go --nhv-demo <file>, or the title's Demos): no
// server and no game - the client is fed the recorded messages at the times
// they came (anything it sends goes nowhere), you're put where you were,
// looking where you looked; each part from when its game began,
// one after the other (the client starting again for each). Space pauses,
// W and S speed it up and slow it down. A demo plays in any VoxHack
// speaking the same network protocol (the same messages) whose demos are
// of the same format (nhv_demo.cpp: DEMO_FORMAT), with the media it used
// in its cache - or, missing there, the game's own of the same name.

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
// ... with a game in it (the server asks: from its thread)
bool recording_game();
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
// ... going through that now (black and silent meanwhile: game.cpp)
bool play_catching_up();
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
// this part's over, the next one's next (the game's ended: it's played in
// a session of its own)
bool play_part_over();
// left from its pause menu: no more of it
void play_leave();
// the session playing a part ended: the settings and state it changed put back
void play_end();
// on to the next part (true), if there's one and it wasn't left
bool play_next_part();
// done with it: the file closed, play_path cleared
void play_close();

// ---------------------------------------------------------------- the list
// (the title's Demos: core.nhv_demo_*, l_nethack.cpp)
typedef std::vector<std::pair<std::string, std::string>> Info;
// the game's details now, from the server as they change (name, role,
// dlvl, game - NetHack's id...): kept beside the demo being recorded, as
// <demo>.info ("key=value" lines). A recording that never had any - only
// the title screen or the prologue, no game - is thrown away as it ends;
// one whose game was continued from an earlier demo is added to that one.
void set_info(const Info &info);
struct Entry {
	std::string file;   // its name in the demos folder
	u64 size = 0;       // bytes
	std::string date;   // when it was recorded, from its name: "2026-10-30 14:05"
	std::string name;   // who recorded it
	bool playable = false;
	std::string why;    // why not
	int format = 1;     // (its format: nhv_demo.cpp)
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
// the session started again when it ends (back to the title from a game
// being recorded: its demo kept, the next game a demo of its own)
void ask_restart();
bool take_restart();

// ---------------------------------------------------------------- media
// the game's own file of that name, for a demo's media missing from the
// cache: its path, "" if there's none
std::string media_fallback(const std::string &name);

} // namespace nhv_demo
