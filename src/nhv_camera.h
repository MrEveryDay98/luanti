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

} // namespace nhv_camera
