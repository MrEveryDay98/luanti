// VoxHack: the Plane of Water's bubbles where they meet the floor.
//
// The game (core.nhv_bubble_floor, nhv_camera.h) says where its bubbles are
// - each an ellipse on the floor, and how fast it's moving - and this draws
// them: one flat sheet over them all, its shader (nhv_bubble_shader)
// lighting each pixel by how far in it is from the edge of the bubbles
// together - bright at the edge, fading inward, nothing outside - so where
// two meet there's one smooth outline round both. Between the game's word
// the bubbles glide on at the speed it gave.

#pragma once

#include "irrlichttypes_bloated.h"
#include <ISceneNode.h>
#include <SMaterial.h>
#include <memory>

class IShaderSource;
class IShaderUniformSetterFactory;

class NhvBubbleFloor : public scene::ISceneNode
{
public:
	NhvBubbleFloor(scene::ISceneManager *mgr, IShaderSource *ssrc);

	void OnRegisterSceneNode() override;
	void render() override;
	const aabb3f &getBoundingBox() const override { return m_box; }
	u32 getMaterialCount() const override { return 1; }
	video::SMaterial &getMaterial(u32 i) override { return m_material; }

	// the camera's offset (nodes), every frame (game.cpp)
	void setCameraOffset(v3s16 offset) { m_camera_offset = offset; }

private:
	video::SMaterial m_material;
	aabb3f m_box{{-1e6f, -1e6f, -1e6f}, {1e6f, 1e6f, 1e6f}}; // (drawn wherever it is)
	v3s16 m_camera_offset;
};

// its shader's uniforms: the bubbles (game.cpp adds it)
std::unique_ptr<IShaderUniformSetterFactory> nhvBubbleUniformSetterFactory();
