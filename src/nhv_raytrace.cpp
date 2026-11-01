// VoxHack: raytraced light (nhv_raytrace.h)

#include "nhv_raytrace.h"
#include <cmath>
#include <mutex>

namespace nhv_rt
{

static std::mutex s_mutex;
static std::vector<u8> s_cells;
static std::vector<u8> s_levels;
static std::vector<Portal> s_portals;
static std::vector<Light> s_lights;
static u32 s_generation = 0;

void set_grid(std::vector<u8> &&cells, std::vector<Portal> &&portals, std::vector<u8> &&levels)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_cells = std::move(cells);
	s_levels = std::move(levels);
	s_portals = std::move(portals);
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
	s_levels.clear();
	s_portals.clear();
	s_lights.clear();
	++s_generation;
}

u32 generation()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_cells.empty() ? 0 : s_generation;
}

void get_grid(std::vector<u8> *cells, std::vector<u8> *levels)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	*cells = s_cells;
	*levels = s_levels;
}

void get_portals(std::vector<Portal> *portals)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	*portals = s_portals;
}

void get_lights(std::vector<Light> *lights)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	*lights = s_lights;
}

// (the lock held)
static u8 cell(int x, int y, int z)
{
	if (x < 0 || x >= GX || z < 0 || z >= GZ || y < Y0 || y >= Y0 + GY)
		return 0;
	size_t i = ((size_t)(y - Y0) * GZ + z) * GX + x;
	return i < s_cells.size() ? s_cells[i] : 0;
}

// nothing that stops light on the straight line from a to b (nodes; not
// the blocks they're in) - stepping block by block, as the shaders do
static bool clear_line(v3f a, v3f b)
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
		float t = std::fmin(tmax[0], std::fmin(tmax[1], tmax[2]));
		if (t >= 1.0f)
			return true;
		// (through an edge or a corner - two ways out of the block at once:
		// the blocks beside it on the way looked at too, so it can't slip
		// between two that only touch there)
		bool hit[3];
		int n = 0;
		for (int k = 0; k < 3; k++) {
			hit[k] = tmax[k] - t < 1e-4f;
			n += hit[k] ? 1 : 0;
		}
		if (n > 1) {
			for (int k = 0; k < 3; k++) {
				if (hit[k] && (cell(c[0] + (k == 0 ? step[0] : 0), c[1] + (k == 1 ? step[1] : 0),
						c[2] + (k == 2 ? step[2] : 0)) & RT_STOPS))
					return false;
			}
		}
		for (int k = 0; k < 3; k++) {
			if (hit[k]) {
				c[k] += step[k];
				tmax[k] += tdelta[k];
			}
		}
		if (c[0] == ce[0] && c[1] == ce[1] && c[2] == ce[2])
			return true;
		if (cell(c[0], c[1], c[2]) & RT_STOPS)
			return false;
	}
	return true;
}

// a point on the line between two blocks, rounding into a solid one: moved
// just into the open one beside it (as the shaders do - a ray from inside a
// block isn't stopped by it)
static v3f into_open(v3f p)
{
	float *c[3] = {&p.X, &p.Y, &p.Z};
	auto solid = [&]() {
		return cell((int)std::floor(p.X + 0.5f), (int)std::floor(p.Y + 0.5f), (int)std::floor(p.Z + 0.5f)) & RT_STOPS;
	};
	for (int k = 0; k < 3; k++) {
		if (!solid())
			break;
		float v = *c[k];
		float f = (v + 0.5f) - std::floor(v + 0.5f);
		float to = f < 0.03f ? v - 0.05f : f > 0.97f ? v + 0.05f : v;
		if (to == v)
			continue;
		*c[k] = to;
		if (solid())
			*c[k] = v;
	}
	return p;
}

float light_at(v3f p)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_cells.empty())
		return 0.0f;
	p = into_open(p);
	if (cell((int)std::floor(p.X + 0.5f), (int)std::floor(p.Y + 0.5f), (int)std::floor(p.Z + 0.5f)) & RT_LIT)
		return FULL;
	float best = 0.0f;
	for (const Portal &po : s_portals) {
		v3f out = dir_vec(po.dir);
		v3f to = p - po.pos;
		if (to.dotProduct(out) <= 0.0f)
			continue;
		float level = FULL - to.getLength() / FALL;
		if (level <= best || level < 1.0f)
			continue;
		if (clear_line(p, portal_point(po)))
			best = level;
	}
	for (const Light &l : s_lights) {
		float level = l.level - std::fmax((p - l.pos).getLength() - 0.5f, 0.0f);
		if (level <= best || level < 1.0f)
			continue;
		if (clear_line(p, l.pos))
			best = level;
	}
	return best;
}

}
