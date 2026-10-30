// VoxHack: where the client's camera is (see nhv_camera.h)

#include "nhv_camera.h"

#include "porting.h"
#include <mutex>

namespace nhv_camera
{

static std::mutex lock;
static v3f where;
static bool known = false;
static float roll = 0.0f;
static v3f drift(0.0f, 0.0f, 0.0f);
static std::vector<Bubble> bubbles;
static float bubbles_y = 0.0f;
static u64 bubbles_at = 0;

void set(v3f pos)
{
	std::lock_guard<std::mutex> guard(lock);
	where = pos;
	known = true;
}

static v3f looking(0, 0, 1);
static bool look_known = false;

void set_look(v3f dir)
{
	std::lock_guard<std::mutex> guard(lock);
	looking = dir;
	look_known = true;
}

bool get_look(v3f *dir)
{
	std::lock_guard<std::mutex> guard(lock);
	if (look_known)
		*dir = looking;
	return look_known;
}

bool get(v3f *pos)
{
	std::lock_guard<std::mutex> guard(lock);
	if (known)
		*pos = where;
	return known;
}

void set_roll(float degrees)
{
	std::lock_guard<std::mutex> guard(lock);
	roll = degrees;
}

float get_roll()
{
	std::lock_guard<std::mutex> guard(lock);
	return roll;
}

void set_drift(v3f velocity)
{
	std::lock_guard<std::mutex> guard(lock);
	drift = velocity;
}

v3f get_drift()
{
	std::lock_guard<std::mutex> guard(lock);
	return drift;
}

void set_bubbles(const std::vector<Bubble> &list, float floor_y)
{
	std::lock_guard<std::mutex> guard(lock);
	bubbles = list;
	bubbles_y = floor_y;
	bubbles_at = porting::getTimeMs();
}

bool get_bubbles(std::vector<Bubble> *list, float *floor_y, u64 *at_ms)
{
	std::lock_guard<std::mutex> guard(lock);
	*list = bubbles;
	*floor_y = bubbles_y;
	*at_ms = bubbles_at;
	return !bubbles.empty();
}

static std::vector<PathPoint> path;
static u64 path_at = 0;

void set_path(const std::vector<PathPoint> &points)
{
	std::lock_guard<std::mutex> guard(lock);
	path = points;
	path_at = porting::getTimeMs();
}

void stop_path()
{
	std::lock_guard<std::mutex> guard(lock);
	path.clear();
}

bool get_path(std::vector<PathPoint> *points, u64 *at_ms)
{
	std::lock_guard<std::mutex> guard(lock);
	*points = path;
	*at_ms = path_at;
	return !path.empty();
}

static Orbit orbit;

void set_orbit(const Orbit &o)
{
	std::lock_guard<std::mutex> guard(lock);
	orbit = o;
	orbit.on = true;
	orbit.at_ms = porting::getTimeMs();
}

void set_orbit_look(float look_y)
{
	std::lock_guard<std::mutex> guard(lock);
	orbit.look_y = look_y;
}

void stop_orbit()
{
	std::lock_guard<std::mutex> guard(lock);
	orbit.on = false;
}

Orbit get_orbit()
{
	std::lock_guard<std::mutex> guard(lock);
	return orbit;
}

} // namespace nhv_camera
