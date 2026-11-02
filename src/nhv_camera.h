// VoxHack: where the client's camera is.
//
// The client notes its camera's position (nodes) every frame; the game
// (server Lua, core.nhv_camera) turns sprites to face it - in third person
// the camera isn't where the player is. Singleplayer: the server and the
// client share this, in one process.

#pragma once

#include "irr_v3d.h"
#include <vector>

namespace nhv_camera
{

// client: the camera is here now (nodes)
void set(v3f pos);
// server: where the camera is (false: not known yet)
bool get(v3f *pos);
// client: the way it's looking now (a unit vector); server: that (false: not
// known yet) - a cutscene's flown camera keeps what's round it loaded
// (intro_play.lua)
void set_look(v3f dir);
bool get_look(v3f *dir);

// server (core.nhv_camera_roll): tilt the view this many degrees to the
// side (0: level) - struggling in the Plane of Air's thin air; client: how
// far it's to be tilted (the camera eases there)
void set_roll(float degrees);
float get_roll();

// server (core.nhv_player_drift): carry the player along at this velocity
// (nodes per second; zero: not) - a bubble of the Plane of Water carrying
// you; client: added to where the local player moves itself, every frame,
// as a moving platform would carry it
void set_drift(v3f velocity);
v3f get_drift();
// server (core.nhv_drift_facing): while carried along so (travel), the
// camera eases round to face the way it goes - until the player turns it
// (client/game.cpp)
void set_drift_facing(bool on);
bool get_drift_facing();
// server (core.nhv_hero_blind): the hero is blind - what's in your hands
// isn't lit (client/clientenvironment.cpp)
void set_blind(bool on);
bool get_blind();

// server (core.nhv_hand_fx): a glow over the items in your hands, pulsing
// (confuse monster's glowing hands, Sting warning of orcs): its colour, how
// strong (0..1) and how fast it pulses (a second; 0: steady). client:
// camera.cpp
struct HandFx {
	u8 r = 0, g = 0, b = 0;
	float amount = 0.0f, hz = 0.0f;
};
void set_hand_fx(const HandFx &fx);
HandFx get_hand_fx();

// server (core.nhv_bubble_floor): the Plane of Water's bubbles, each an
// ellipse on the floor - x, z (its middle), rx, rz (its half widths), vx,
// vz (how fast it's going) - all in nodes; floor_y: the floor's height;
// none: not there. client (nhv_bubble_floor.cpp): them, and when they were
// said (ms, porting::getTimeMs)
struct Bubble {
	float x, z, rx, rz, vx, vz;
};
void set_bubbles(const std::vector<Bubble> &list, float floor_y);
bool get_bubbles(std::vector<Bubble> *list, float *floor_y, u64 *at_ms);

// server (core.nhv_camera_orbit): the camera circles a point, looking at it
// - a cutscene's (the ascension: cutscene.lua) - placed by the client every
// frame, so it goes round smoothly: center (nodes), radius and height over
// it (nodes), speed (radians a second), the angle it starts at, and how far
// over the center it looks (look_y: the client eases there, so it can be
// moved along). Off: the camera is the player's again.
struct Orbit {
	bool on = false;
	v3f center;
	float radius = 5, height = 4, speed = 0, angle = 0, look_y = 1;
	u64 at_ms = 0; // (when it started: the client's angle goes on from there)
};
// server (core.nhv_camera_path): the camera flown along a path - a
// cutscene's (the game's opening: intro_play.lua) - placed by the client
// every frame, so it moves smoothly: points with when it's there (seconds
// from the start), where (the eyes, nodes) and how it looks (yaw, pitch:
// the player's look, radians); a smooth curve through them, but a point
// that's a cut is jumped to at its time. After the last it stays there.
// Off: the camera is the player's again.
struct PathPoint {
	float t = 0;
	v3f pos;
	float yaw = 0, pitch = 0;
	bool cut = false;
};
void set_path(const std::vector<PathPoint> &points);
void stop_path();
// (false: no path; at_ms: when it started)
bool get_path(std::vector<PathPoint> *points, u64 *at_ms);

void set_orbit(const Orbit &o);
void set_orbit_look(float look_y);
void stop_orbit();
Orbit get_orbit();

} // namespace nhv_camera
