// VoxHack: the Plane of Water's bubbles where they meet the floor (see
// nhv_bubble_floor.h)

#include "nhv_bubble_floor.h"
#include "nhv_camera.h"
#include "constants.h"
#include "porting.h"
#include "util/numeric.h"
#include "client/shader.h"
#include <ISceneManager.h>
#include <IVideoDriver.h>
#include <S3DVertex.h>
#include <IMaterialRendererServices.h>

namespace {

// (as many as the shader takes: nhv_bubble_shader's NHV_BUBBLES)
constexpr int MAX_BUBBLES = 32;

struct NhvBubbleUniforms {
	f32 bubbles[MAX_BUBBLES * 4] = {}; // x, z (nodes), rx, rz
	f32 count = 0;
} g_nhv_bubbles;

class NhvBubbleUniformSetter : public IShaderUniformSetter {
	CachedPixelShaderSetting<f32, MAX_BUBBLES * 4> m_bubbles{"nhvBubbles"};
	CachedPixelShaderSetting<f32> m_count{"nhvBubbleCount"};
public:
	void onSetUniforms(video::IMaterialRendererServices *services) override
	{
		m_bubbles.set(g_nhv_bubbles.bubbles, services);
		m_count.set(&g_nhv_bubbles.count, services);
	}
};

class NhvBubbleUniformSetterFactory : public IShaderUniformSetterFactory {
public:
	IShaderUniformSetter *create(const std::string &name) override
	{
		return name == "nhv_bubble_shader" ? new NhvBubbleUniformSetter() : nullptr;
	}
};

} // namespace

std::unique_ptr<IShaderUniformSetterFactory> nhvBubbleUniformSetterFactory()
{
	return std::make_unique<NhvBubbleUniformSetterFactory>();
}

NhvBubbleFloor::NhvBubbleFloor(scene::ISceneManager *mgr, IShaderSource *ssrc) :
		scene::ISceneNode(mgr->getRootSceneNode(), mgr, -1)
{
	m_material.MaterialType =
			ssrc->getShaderInfo(ssrc->getShaderRaw("nhv_bubble_shader", true)).material;
	m_material.BackfaceCulling = false;
	m_material.ZWriteEnable = video::EZW_OFF;
	m_material.FogEnable = true;
	// (drawn where it is: no culling by a box)
	setAutomaticCulling(scene::EAC_OFF);
}

void NhvBubbleFloor::OnRegisterSceneNode()
{
	if (IsVisible)
		SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
	ISceneNode::OnRegisterSceneNode();
}

void NhvBubbleFloor::render()
{
	if (SceneManager->getSceneNodeRenderPass() != scene::ESNRP_TRANSPARENT)
		return;
	std::vector<nhv_camera::Bubble> list;
	f32 floor_y;
	u64 at_ms;
	if (!nhv_camera::get_bubbles(&list, &floor_y, &at_ms) || list.empty())
		return;

	// where they are now: glided on from where the game last said, at the
	// speed it gave (not too far: it says again soon)
	const f32 dt = MYMIN((porting::getTimeMs() - at_ms) / 1000.0f, 0.25f);
	int n = 0;
	f32 x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
	for (const auto &b : list) {
		if (n >= MAX_BUBBLES)
			break;
		const f32 x = b.x + b.vx * dt, z = b.z + b.vz * dt;
		g_nhv_bubbles.bubbles[4 * n] = x;
		g_nhv_bubbles.bubbles[4 * n + 1] = z;
		g_nhv_bubbles.bubbles[4 * n + 2] = b.rx;
		g_nhv_bubbles.bubbles[4 * n + 3] = b.rz;
		x0 = MYMIN(x0, x - b.rx), x1 = MYMAX(x1, x + b.rx);
		z0 = MYMIN(z0, z - b.rz), z1 = MYMAX(z1, z + b.rz);
		n++;
	}
	g_nhv_bubbles.count = (f32)n;

	// one flat sheet over them all, its corners' places in the world (nodes)
	// in its texture coordinates - the shader works by those
	const f32 m = 0.5f;
	x0 -= m, x1 += m, z0 -= m, z1 += m;
	const v3f off = intToFloat(m_camera_offset, BS);
	const f32 y = floor_y * BS - off.Y;
	const video::SColor c(255, 255, 255, 255);
	video::S3DVertex v[4] = {
		video::S3DVertex(x0 * BS - off.X, y, z0 * BS - off.Z, 0, 1, 0, c, x0, z0),
		video::S3DVertex(x1 * BS - off.X, y, z0 * BS - off.Z, 0, 1, 0, c, x1, z0),
		video::S3DVertex(x1 * BS - off.X, y, z1 * BS - off.Z, 0, 1, 0, c, x1, z1),
		video::S3DVertex(x0 * BS - off.X, y, z1 * BS - off.Z, 0, 1, 0, c, x0, z1),
	};
	const u16 indices[] = {0, 1, 2, 0, 2, 3};

	video::IVideoDriver *driver = SceneManager->getVideoDriver();
	driver->setTransform(video::ETS_WORLD, core::IdentityMatrix);
	driver->setMaterial(m_material);
	driver->drawIndexedTriangleList(v, 4, indices, 2);
}
