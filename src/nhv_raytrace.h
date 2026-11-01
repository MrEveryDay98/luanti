// VoxHack: raytraced light (the setting nhv_raytrace).
//
// The game (server Lua, core.nhv_rt_scan) has the level's blocks looked at
// - which stop light, which are in a lit room - and where its lit rooms
// open onto the dark (portals: the faces between a lit room's block and an
// open one outside it, at standing height); and (core.nhv_rt_lights) where
// its lamps are. The client puts the blocks in a texture and the shaders
// trace rays through it: a lit room lit through and through; out of it,
// light only where a straight line reaches one of its portals - shafts out
// of doorways, with straight edges - fading with distance; the hero's
// light and lamps casting hard shadows. Singleplayer: the server and the
// client share this, in one process.

#pragma once

#include "irr_v3d.h"
#include "irrlichttypes.h"
#include <cmath>
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

struct Portal {
	v3f pos; // the face's bottom middle (nodes: at standing height)
	int dir; // the way out: 0 +x, 1 -x, 2 +z, 3 -z
	int height; // how many blocks up it's open (1..MAX_UP)
};
// (how far up a portal's looked at: blocks from standing height)
constexpr int MAX_UP = 4;
struct Light {
	v3f pos; // nodes
	float level;
};

// server: the blocks (GX * GY * GZ bits, x fastest, then z, then y), the
// portals
void set_grid(std::vector<u8> &&cells, std::vector<Portal> &&portals);
// server: the lamps
void set_lights(std::vector<Light> &&lights);
// server: none (left the game)
void clear();

// client: the grid's version (0: none yet) - new blocks to upload when it
// changes; copies of what's there
u32 generation();
void get_grid(std::vector<u8> *cells);
void get_portals(std::vector<Portal> *portals);
void get_lights(std::vector<Light> *lights);

// the light level traced to a point (nodes) from the lit rooms and the
// lamps (not the hero's light): for things not drawn by the nodes' shader
// (entities, your hand)
float light_at(v3f pos);

// the points a portal's light is traced to: up both its edges, from its
// bottom to its top - a point that sees any of a doorway is lit by it (hard
// shadows, but none where the doorway's in sight)
constexpr int MAX_POINTS = (MAX_UP + 1) * 2;
inline int portal_points(const Portal &po, v3f *pts)
{
	v3f out = po.dir == 0 ? v3f(1, 0, 0) : po.dir == 1 ? v3f(-1, 0, 0) : po.dir == 2 ? v3f(0, 0, 1) : v3f(0, 0, -1);
	v3f across(std::fabs(out.Z), 0, std::fabs(out.X));
	int h = po.height < MAX_UP ? po.height : MAX_UP, n = 0;
	for (int j = 0; j <= h; j++)
		for (int s = -1; s <= 1; s += 2)
			pts[n++] = po.pos + v3f(0, -0.45f + j * (h - 0.1f) / h, 0) + across * (0.45f * s) + out * 0.01f;
	return n;
}

inline v3f dir_vec(int dir)
{
	return dir == 0 ? v3f(1, 0, 0) : dir == 1 ? v3f(-1, 0, 0) : dir == 2 ? v3f(0, 0, 1) : v3f(0, 0, -1);
}

}
