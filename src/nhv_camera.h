// VoxHack: where the client's camera is.
//
// The client notes its camera's position (nodes) every frame; the game
// (server Lua, core.nhv_camera) turns sprites to face it - in third person
// the camera isn't where the player is. Singleplayer: the server and the
// client share this, in one process.

#pragma once

#include "irr_v3d.h"

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

} // namespace nhv_camera
