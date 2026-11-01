// VoxHack: raytraced light (nhv_raytrace.h)

#include "nhv_raytrace.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace nhv_rt
{

static std::mutex s_mutex;
static std::vector<u8> s_cells;
static std::vector<u8> s_glow;
static std::vector<Light> s_lights;
static u32 s_generation = 0;

static u8 cell(const std::vector<u8> &cells, int x, int y, int z)
{
	if (x < 0 || x >= GX || z < 0 || z >= GZ || y < Y0 || y >= Y0 + GY)
		return 0;
	size_t i = ((size_t)(y - Y0) * GZ + z) * GX + x;
	return i < cells.size() ? cells[i] : 0;
}

// nothing that stops light on the straight line from a to b (nodes; not
// the blocks they're in) - stepping block by block, as the shaders do
static bool clear_line(const std::vector<u8> &cells, v3f a, v3f b)
{
	float q[3] = {a.X + 0.5f, a.Y + 0.5f, a.Z + 0.5f};
	float e[3] = {b.X + 0.5f, b.Y + 0.5f, b.Z + 0.5f};
	int c[3], ce[3], step[3];
	float tmax[3], tdelta[3];
	for (int k = 0; k < 3; k++) {
		float d = e[k] - q[k];
		c[k] = (int)std::floor(q[k]);
		ce[k] = (int)std::floor(e[k]);
		step[k] = d > 0 ? 1 : d < 0 ? -1 : 0;
		float ad = std::fabs(d);
		tdelta[k] = ad > 1e-6f ? 1.0f / ad : 1e9f;
		tmax[k] = step[k] > 0 ? (c[k] + 1 - q[k]) * tdelta[k]
				: step[k] < 0 ? (q[k] - c[k]) * tdelta[k] : 1e9f;
	}
	for (int i = 0; i < 96; i++) {
		int k = tmax[0] < tmax[1] ? (tmax[0] < tmax[2] ? 0 : 2) : (tmax[1] < tmax[2] ? 1 : 2);
		if (tmax[k] >= 1.0f)
			return true;
		c[k] += step[k];
		tmax[k] += tdelta[k];
		if (c[0] == ce[0] && c[1] == ce[1] && c[2] == ce[2])
			return true;
		if (cell(cells, c[0], c[1], c[2]) & RT_STOPS)
			return false;
	}
	return true;
}

static v3f dir_vec(int dir)
{
	return dir == 0 ? v3f(1, 0, 0) : dir == 1 ? v3f(-1, 0, 0) : dir == 2 ? v3f(0, 0, 1) : v3f(0, 0, -1);
}

// The doorways' light, block by block: a lit room's blocks full; an open
// block outside, out in front of a portal and near enough, as bright as
// that far from it makes it times how much of the portal it sees - traced
// to points over the portal's face, its height and width (light through
// all of a doorway, not one point in it). From standing height a few
// blocks down (into pits) and up (to the ceiling).
static std::vector<u8> work_out_glow(const std::vector<u8> &cells, const std::vector<Portal> &portals,
		int stand)
{
	std::vector<u8> glow(cells.size(), 0);
	for (size_t i = 0; i < cells.size(); i++)
		if (cells[i] & RT_LIT)
			glow[i] = 255;
	const int ylo = std::max(Y0, stand - 3), yhi = std::min(Y0 + GY - 1, stand + 5);
	const int R = (int)RANGE;
	for (const Portal &po : portals) {
		v3f out = dir_vec(po.dir);
		v3f across(std::fabs(out.Z), 0, std::fabs(out.X));
		v3f pts[MAX_UP * 3];
		int n = 0;
		for (int k = 0; k < po.height && k < MAX_UP; k++)
			for (int s = -1; s <= 1; s++)
				pts[n++] = po.pos + v3f(0, (float)k, 0) + across * (0.4f * s) + out * 0.01f;
		int px = (int)std::floor(po.pos.X + 0.5f), pz = (int)std::floor(po.pos.Z + 0.5f);
		for (int y = ylo; y <= yhi; y++)
		for (int z = std::max(0, pz - R); z <= std::min(GZ - 1, pz + R); z++)
		for (int x = std::max(0, px - R); x <= std::min(GX - 1, px + R); x++) {
			size_t i = ((size_t)(y - Y0) * GZ + z) * GX + x;
			if (cells[i] != 0)
				continue;
			v3f p((float)x, (float)y, (float)z);
			v3f to = p - (po.pos + v3f(0, (po.height - 1) * 0.5f, 0));
			if ((p - po.pos).dotProduct(out) <= 0.0f)
				continue;
			float level = FULL - to.getLength() / FALL;
			if (level < 1.0f || level / FULL * 255.0f <= glow[i])
				continue;
			int seen = 0;
			for (int k = 0; k < n; k++)
				seen += clear_line(cells, p, pts[k]) ? 1 : 0;
			u8 v = (u8)std::round(level / FULL * seen / n * 255.0f);
			glow[i] = std::max(glow[i], v);
		}
	}
	return glow;
}

void set_grid(std::vector<u8> &&cells, const std::vector<Portal> &portals, int stand)
{
	std::vector<u8> glow = work_out_glow(cells, portals, stand);
	std::lock_guard<std::mutex> lock(s_mutex);
	s_cells = std::move(cells);
	s_glow = std::move(glow);
	++s_generation;
	if (s_generation == 0)
		s_generation = 1;
}

void set_lights(std::vector<Light> &&lights)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_lights = std::move(lights);
}

void clear()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_cells.clear();
	s_glow.clear();
	s_lights.clear();
	++s_generation;
}

u32 generation()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_cells.empty() ? 0 : s_generation;
}

void get_grid(std::vector<u8> *cells, std::vector<u8> *glow)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	*cells = s_cells;
	*glow = s_glow;
}

void get_lights(std::vector<Light> *lights)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	*lights = s_lights;
}

float light_at(v3f p)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_cells.empty())
		return 0.0f;
	int x = (int)std::floor(p.X + 0.5f), y = (int)std::floor(p.Y + 0.5f), z = (int)std::floor(p.Z + 0.5f);
	float best = 0.0f;
	if (x >= 0 && x < GX && z >= 0 && z < GZ && y >= Y0 && y < Y0 + GY) {
		size_t i = ((size_t)(y - Y0) * GZ + z) * GX + x;
		if (i < s_glow.size())
			best = s_glow[i] / 255.0f * FULL;
	}
	for (const Light &l : s_lights) {
		float level = l.level - std::fmax((p - l.pos).getLength() - 0.5f, 0.0f);
		if (level <= best || level < 1.0f)
			continue;
		if (clear_line(s_cells, p, l.pos))
			best = level;
	}
	return best;
}

}
