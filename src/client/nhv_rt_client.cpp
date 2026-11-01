// VoxHack: the client's side of the raytraced light (nhv_rt_client.h)

#include "nhv_rt_client.h"
#include "nhv_raytrace.h"
#include "settings.h"
#include "porting.h"
#include <IVideoDriver.h>
#include <IImage.h>
#include <ITexture.h>
#include <algorithm>
#include <vector>

namespace nhv_rt_client
{

static bool s_enabled = false;
static u64 s_enabled_read = 0;
static video::ITexture *s_texture = nullptr;
static video::IVideoDriver *s_driver = nullptr;
static u32 s_generation = 0;
static float s_portals[MAX_PORTALS * 4];
static float s_portal_count = 0.0f;
static float s_lights[MAX_LIGHTS * 4];
static float s_light_count = 0.0f;

bool enabled()
{
	u64 now = porting::getTimeMs();
	if (now - s_enabled_read > 250) {
		s_enabled_read = now;
		s_enabled = g_settings->getBool("nhv_raytrace");
	}
	return s_enabled;
}

static void upload(video::IVideoDriver *driver)
{
	using namespace nhv_rt;
	std::vector<u8> cells;
	get_grid(&cells);
	if (s_texture) {
		s_driver->removeTexture(s_texture);
		s_texture = nullptr;
	}
	if (cells.empty())
		return;
	// (y layers one under another: x across, z + layer * GZ down)
	video::IImage *img = driver->createImage(video::ECF_A8R8G8B8,
			core::dimension2d<u32>(GX, GZ * GY));
	u32 *px = (u32 *)img->getData();
	for (size_t i = 0; i < cells.size() && i < (size_t)GX * GZ * GY; i++) {
		u8 c = cells[i];
		px[i] = 0xFF000000u | ((c & RT_STOPS) ? 0x00FF0000u : 0u) | ((c & RT_LIT) ? 0x0000FF00u : 0u);
	}
	bool mips = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, false);
	s_texture = driver->addTexture("nhv_rt_grid", img);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mips);
	s_driver = driver;
	img->drop();
}

void update(video::IVideoDriver *driver, v3f camera)
{
	if (!enabled()) {
		s_portal_count = s_light_count = 0.0f;
		return;
	}
	u32 gen = nhv_rt::generation();
	if (gen != s_generation) {
		s_generation = gen;
		upload(driver);
	}
	// (the nearest the camera: those near enough to light what's in view)
	std::vector<nhv_rt::Portal> portals;
	nhv_rt::get_portals(&portals);
	auto near_first = [&](const v3f &a, const v3f &b) {
		return (a - camera).getLengthSQ() < (b - camera).getLengthSQ();
	};
	size_t np = std::min(portals.size(), (size_t)MAX_PORTALS);
	std::partial_sort(portals.begin(), portals.begin() + np, portals.end(),
			[&](const nhv_rt::Portal &a, const nhv_rt::Portal &b) { return near_first(a.pos, b.pos); });
	for (size_t i = 0; i < np; i++) {
		s_portals[i * 4] = portals[i].pos.X;
		s_portals[i * 4 + 1] = portals[i].pos.Y;
		s_portals[i * 4 + 2] = portals[i].pos.Z;
		// (the way out, how far up it's open, how wide it is: dir + 4 *
		// (height - 1) + 16 * (width - 1))
		s_portals[i * 4 + 3] = (float)(portals[i].dir + 4 * (portals[i].height - 1)
				+ 16 * (portals[i].width - 1));
	}
	s_portal_count = (float)np;
	std::vector<nhv_rt::Light> lights;
	nhv_rt::get_lights(&lights);
	size_t nl = std::min(lights.size(), (size_t)MAX_LIGHTS);
	std::partial_sort(lights.begin(), lights.begin() + nl, lights.end(),
			[&](const nhv_rt::Light &a, const nhv_rt::Light &b) { return near_first(a.pos, b.pos); });
	for (size_t i = 0; i < nl; i++) {
		s_lights[i * 4] = lights[i].pos.X;
		s_lights[i * 4 + 1] = lights[i].pos.Y;
		s_lights[i * 4 + 2] = lights[i].pos.Z;
		s_lights[i * 4 + 3] = lights[i].level;
	}
	s_light_count = (float)nl;
}

video::ITexture *texture()
{
	return enabled() ? s_texture : nullptr;
}

const float *portals()
{
	return s_portals;
}

float portal_count()
{
	return s_portal_count;
}

const float *lights()
{
	return s_lights;
}

float light_count()
{
	return s_light_count;
}

}
