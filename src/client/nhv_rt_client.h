// VoxHack: the client's side of the raytraced light (nhv_raytrace.h): the
// level's blocks and the doorways' light in a texture for the shaders
// (ClientMap gives it to the nodes' materials, as texture2), and the lamps
// nearest the camera to trace to (game.cpp's shader settings)

#pragma once

#include "irr_v3d.h"
#include "irrlichttypes.h"

namespace video {
class IVideoDriver;
class ITexture;
}

namespace nhv_rt_client
{

constexpr int MAX_LIGHTS = 8;

// the setting nhv_raytrace (read a few times a second)
bool enabled();

// once a frame, before the map's drawn: the texture brought up to date,
// the nearest lamps to camera (nodes) picked
void update(video::IVideoDriver *driver, v3f camera);

// the blocks' texture (nullptr: none, or off)
video::ITexture *texture();

// for the shaders: the lamps (x, y, z, level - nodes) and how many
const float *lights();
float light_count();

}
