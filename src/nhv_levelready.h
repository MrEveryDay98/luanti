// VoxHack: is a newly built level on the screen yet?
//
// The game (server Lua, core.nh_level_await) names the region it has just
// rebuilt; the client notes each map block of it that arrives after that,
// and each one whose mesh is then made. The level is ready when every
// block of the region has arrived and been meshed (core.nh_level_ready).
// Singleplayer: the server and the client share this, in one process.

#pragma once

#include "irr_v3d.h"

namespace nhv_levelready
{

// server: the region (nodes) just rebuilt; start waiting for the part of
// it the client is sent: its blocks within radius nodes of center (the
// client's view range, cut short by the fog)
void await(v3s16 minp, v3s16 maxp, v3s16 center, s16 radius);
// server: how much of it is on the screen, 0..1 (1 when not waiting);
// received: how much of it has arrived (meshed or not)
float progress(float *received = nullptr);

// client: a map block arrived / its mesh was made
void block_received(v3s16 blockpos);
void block_meshed(v3s16 blockpos);

} // namespace nhv_levelready
