// VoxHack: the client's side of the raytraced light (nhv_raytrace.h): the
// level's blocks in a texture for the shaders (ClientMap gives it to the
// nodes' materials, as texture2), and the portals and lamps nearest the
// camera to trace to (game.cpp's shader settings)

#pragma once

#include "irr_v3d.h"
#include "irrlichttypes.h"

namespace video {
class IVideoDriver;
class ITexture;
}

namespace nhv_rt_client
{

constexpr int MAX_PORTALS = 64, MAX_LIGHTS = 8;

// the setting nhv_raytrace (read a few times a second)
bool enabled();
// the setting nhv_square_light (not off): the blocks' own light wanted too
bool square();
// raytracing, with the blocks to trace through
bool raytracing();

// once a frame, before the map's drawn: the texture brought up to date,
// the nearest portals and lamps to camera (nodes) picked
void update(video::IVideoDriver *driver, v3f camera);

// the blocks' texture (nullptr: none, or neither raytraced nor square
// light)
video::ITexture *texture();

// for the shaders: the portals (x, y, z, the way out 0..3) and how many;
// the lamps (x, y, z, level) and how many - nodes
const float *portals();
float portal_count();
const float *lights();
float light_count();

}
