// VoxHack: where the client's camera is (see nhv_camera.h)

#include "nhv_camera.h"

#include <mutex>

namespace nhv_camera
{

static std::mutex lock;
static v3f where;
static bool known = false;
static float roll = 0.0f;

void set(v3f pos)
{
	std::lock_guard<std::mutex> guard(lock);
	where = pos;
	known = true;
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

} // namespace nhv_camera
