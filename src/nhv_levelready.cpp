// VoxHack: is a newly built level on the screen yet? (see nhv_levelready.h)

#include "nhv_levelready.h"
#include "constants.h"
#include "mapblock.h"
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <unordered_set>

namespace nhv_levelready
{

namespace {

std::mutex g_mutex;
bool g_waiting = false;
// the map blocks waited for; of them, the ones that arrived and were meshed
std::unordered_set<u64> g_wanted, g_received, g_meshed;

u64 key(v3s16 p)
{
	return ((u64)(u16)p.X << 32) | ((u64)(u16)p.Y << 16) | (u64)(u16)p.Z;
}

bool inside(v3s16 p)
{
	return g_wanted.count(key(p)) > 0;
}

} // namespace

void await(v3s16 minp, v3s16 maxp, v3s16 center, s16 radius)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	v3s16 b0 = getNodeBlockPos(minp), b1 = getNodeBlockPos(maxp);
	v3s16 c = getNodeBlockPos(center);
	// (the server sends blocks out to a box this many blocks round you)
	s16 r = std::max<s16>((radius + MAP_BLOCKSIZE - 1) / MAP_BLOCKSIZE, 2);
	g_wanted.clear();
	for (s16 z = b0.Z; z <= b1.Z; z++)
	for (s16 y = b0.Y; y <= b1.Y; y++)
	for (s16 x = b0.X; x <= b1.X; x++)
		if (std::abs(x - c.X) <= r && std::abs(y - c.Y) <= r && std::abs(z - c.Z) <= r)
			g_wanted.insert(key(v3s16(x, y, z)));
	g_received.clear();
	g_meshed.clear();
	g_waiting = true;
}

float progress(float *received)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (received)
		*received = 1.0f;
	if (!g_waiting)
		return 1.0f;
	size_t total = std::max<size_t>(g_wanted.size(), 1);
	if (received)
		*received = (float)g_received.size() / total;
	if (g_meshed.size() >= total) {
		g_waiting = false;
		return 1.0f;
	}
	return (float)g_meshed.size() / total;
}

void block_received(v3s16 p)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_waiting && inside(p)) {
		g_received.insert(key(p));
		g_meshed.erase(key(p)); // (its mesh is made again from what came)
	}
}

void block_meshed(v3s16 p)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_waiting && inside(p) && g_received.count(key(p)))
		g_meshed.insert(key(p));
}

} // namespace nhv_levelready
