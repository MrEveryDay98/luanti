// VoxHack: where the client's camera is (see nhv_camera.h)

#include "nhv_camera.h"

#include <mutex>

namespace nhv_camera
{

static std::mutex lock;
static v3f where;
static bool known = false;

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

} // namespace nhv_camera
