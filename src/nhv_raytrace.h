// VoxHack: raytraced light (the setting nhv_raytrace).
//
// The game (server Lua, core.nhv_rt_scan) has the level's blocks looked at
// - which stop light, which are in a lit room - and where its lit rooms
// open onto the dark (portals: the faces between a lit room's block and an
// open one outside it, at standing height); and (core.nhv_rt_lights) where
// its lamps are. Then the doorways' light is traced, block by block: how
// much of each doorway a block's middle sees, how far away it is - light
// going out of a lit room only in straight lines, shafts out of its
// doorways fading as they go. The client puts the blocks and that light in
// a texture; the shaders blend the light smoothly between the blocks round
// each point (as Luanti's smooth light does), and trace rays through the
// blocks for lamps and the hero's light, casting hard shadows.
// Singleplayer: the server and the client share this, in one process.

#pragma once

#include "irr_v3d.h"
#include "irrlichttypes.h"
#include <vector>

namespace nhv_rt
{

// the box of blocks traced through: x 0..GX-1, z 0..GZ-1, y Y0..Y0+GY-1
constexpr int GX = 256, GZ = 64, GY = 32, Y0 = -9;
// a block's bits
constexpr u8 RT_STOPS = 1, RT_LIT = 2;
// a lit room's light; how many blocks out of it it loses a level; how far
// it reaches
constexpr float FULL = 14.0f, FALL = 2.0f, RANGE = 26.0f;
// (how far up a portal's looked at: blocks from standing height)
constexpr int MAX_UP = 4;

struct Portal {
	v3f pos; // the face's bottom middle (nodes: at standing height)
	int dir; // the way out: 0 +x, 1 -x, 2 +z, 3 -z
	int height; // how many blocks up it's open (1..MAX_UP)
};
struct Light {
	v3f pos; // nodes
	float level;
};

// server: the blocks (GX * GY * GZ bits, x fastest, then z, then y) and
// the portals, at standing height stand: the doorways' light worked out
void set_grid(std::vector<u8> &&cells, const std::vector<Portal> &portals, int stand);
// server: the lamps
void set_lights(std::vector<Light> &&lights);
// server: none (left the game)
void clear();

// client: the grid's version (0: none yet) - new blocks to upload when it
// changes; copies of the blocks and their light from the doorways (0..255:
// a lit room's 255), and of the lamps
u32 generation();
void get_grid(std::vector<u8> *cells, std::vector<u8> *glow);
void get_lights(std::vector<Light> *lights);

// the light level at a point (nodes) from the lit rooms and the lamps
// (not the hero's light): for things not drawn by the nodes' shader
// (entities, your hand)
float light_at(v3f pos);

}
