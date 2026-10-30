// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#include "plain.h"
#include "secondstage.h"
#include "settings.h"
#include "client/camera.h"
#include "client/client.h"
#include "client/clientmap.h"
#include "client/hud.h"
#include "client/minimap.h"
#include "client/shadows/dynamicshadowsrender.h"
#include <IGUIEnvironment.h>
#include "gui/guiFormSpecMenu.h"
#include "gui/mainmenumanager.h"

/// Draw3D pipeline step
void Draw3D::run(PipelineContext &context)
{
	if (m_target)
		m_target->activate(context);

	context.device->getSceneManager()->drawAll();
	context.device->getVideoDriver()->setTransform(video::ETS_WORLD, core::IdentityMatrix);
	if (context.show_hud) {
		context.hud->drawBlockBounds();
		context.hud->drawSelectionMesh();
	}
	extern bool nhv_retro;
	auto *from = dynamic_cast<TextureBufferOutput *>(m_target);
	auto *to = dynamic_cast<TextureBufferOutput *>(m_depth_copy);
	video::IRenderTarget *world = from ? from->peekIrrRenderTarget() : nullptr;
	if (nhv_retro && world && to) {
		// (the copy made ready - that activates it - the world copied into
		// it, then the world's picture drawn on again, not cleared)
		auto *driver = context.device->getVideoDriver();
		video::IRenderTarget *copy = to->getIrrRenderTarget(context);
		driver->blitRenderTarget(world, copy);
		driver->setRenderTargetEx(world, video::ECBF_NONE);
	}
	if (m_with_wield && context.draw_wield_tool)
		context.client->getCamera()->drawWieldedTool();
}

void DrawWield::run(PipelineContext &context)
{
	if (m_target)
		m_target->activate(context);

	if (context.draw_wield_tool)
		context.client->getCamera()->drawWieldedTool();
}

void DrawHUD::run(PipelineContext &context)
{
	if (context.show_hud) {
		if (context.shadow_renderer)
			context.shadow_renderer->drawDebug();

		context.hud->resizeHotbar();

		if (context.draw_crosshair)
			context.hud->drawCrosshair();
	}

	context.hud->drawLuaElements(context.client->getCamera()->getOffset(), !context.show_hud);

	if (context.show_hud) {
		context.client->getCamera()->drawNametags();
	}

	context.device->getGUIEnvironment()->drawAll();

	// NetHack Voxel: HUD elements that stay readable above menus (not above
	// the pause menu: the game is paused then and can't hide them)
	auto *top = dynamic_cast<GUIFormSpecMenu *>(g_menumgr.tryGetTopMenu());
	if (!(top && top->doPause))
		context.hud->drawLuaElements(context.client->getCamera()->getOffset(), !context.show_hud, true);
}


void MapPostFxStep::setRenderTarget(RenderTarget * _target)
{
	target = _target;
}

void MapPostFxStep::run(PipelineContext &context)
{
	if (target)
		target->activate(context);

	context.client->getEnv().getClientMap().renderPostFx(context.client->getCamera()->getCameraMode());
}

void RenderShadowMapStep::run(PipelineContext &context)
{
	// This is necessary to render shadows for animations correctly
	context.device->getSceneManager()->getRootSceneNode()->OnAnimate(context.device->getTimer()->getTime());
	context.shadow_renderer->update();
}

// class UpscaleStep

void UpscaleStep::run(PipelineContext &context)
{
	video::ITexture *lowres = m_source->getTexture(0);
	m_target->activate(context);
	context.device->getVideoDriver()->draw2DImage(lowres,
			core::rect<s32>(0, 0, context.target_size.X, context.target_size.Y),
			core::rect<s32>(0, 0, lowres->getSize().Width, lowres->getSize().Height));
}

std::unique_ptr<RenderStep> create3DStage(Client *client, v2f scale, bool with_wield)
{
	RenderStep *step = new Draw3D(with_wield);
	if (g_settings->getBool("enable_post_processing")) {
		RenderPipeline *pipeline = new RenderPipeline();
		pipeline->addStep(pipeline->own(std::unique_ptr<RenderStep>(step)));

		auto effect = addPostProcessing(pipeline, step, scale, client);
		effect->setRenderTarget(pipeline->getOutput());
		step = pipeline;
	}
	return std::unique_ptr<RenderStep>(step);
}

// NetHack Voxel: full resolution while the hud flag nhv_full_res is set
// (renderingengine.cpp rebuilds the pipeline when it changes) - and in
// retro mode, where the screen is 800x600 already (renderingengine.cpp)
bool nhv_full_res = false;
bool nhv_retro = false;

static v2f getDownscaleFactor()
{
	u16 undersampling = nhv_full_res || nhv_retro ? 1 : MYMAX(g_settings->getU16("undersampling"), 1);
	return v2f(1.0f / undersampling);
}

RenderStep* addUpscaling(RenderPipeline *pipeline, RenderStep *previousStep, v2f downscale_factor, Client *client)
{
	const int TEXTURE_LOWRES_COLOR = 0;
	const int TEXTURE_LOWRES_DEPTH = 1;

	if (downscale_factor.X == 1.0f && downscale_factor.Y == 1.0f)
		return previousStep;

	// post-processing pipeline takes care of rescaling
	if (g_settings->getBool("enable_post_processing"))
		return previousStep;

	auto driver = client->getSceneManager()->getVideoDriver();
	video::ECOLOR_FORMAT color_format = selectColorFormat(driver);
	video::ECOLOR_FORMAT depth_format = selectDepthFormat(driver);

	// Initialize buffer
	TextureBuffer *buffer = pipeline->createOwned<TextureBuffer>();
	buffer->setTexture(TEXTURE_LOWRES_COLOR, downscale_factor, "lowres_color", color_format);
	buffer->setTexture(TEXTURE_LOWRES_DEPTH, downscale_factor, "lowres_depth", depth_format);

	// Attach previous step to the buffer
	TextureBufferOutput *buffer_output = pipeline->createOwned<TextureBufferOutput>(
			buffer, std::vector<u8> {TEXTURE_LOWRES_COLOR}, TEXTURE_LOWRES_DEPTH);
	previousStep->setRenderTarget(buffer_output);

	// Add upscaling step
	RenderStep *upscale = pipeline->createOwned<UpscaleStep>();
	upscale->setRenderSource(buffer);
	pipeline->addStep(upscale);

	return upscale;
}

void populatePlainPipeline(RenderPipeline *pipeline, Client *client)
{
	auto downscale_factor = getDownscaleFactor();
	// NetHack Voxel: the wielded item is drawn with the world (not after it,
	// at full resolution over the scaled-up picture): the render resolution
	// setting applies to it too
	auto step3D = pipeline->own(create3DStage(client, downscale_factor, true));
	pipeline->addStep(step3D);
	pipeline->addStep<MapPostFxStep>();

	step3D = addUpscaling(pipeline, step3D, downscale_factor, client);

	step3D->setRenderTarget(pipeline->createOwned<ScreenTarget>());

	pipeline->addStep<DrawHUD>();
}

video::ECOLOR_FORMAT selectColorFormat(video::IVideoDriver *driver)
{
	u32 bits = g_settings->getU32("post_processing_texture_bits");
	if (bits >= 16 && driver->queryTextureFormat(video::ECF_A16B16G16R16F) &&
		driver->queryFeature(video::EVDF_RENDER_TO_FLOAT_TEXTURE))
		return video::ECF_A16B16G16R16F;
	if (bits >= 10 && driver->queryTextureFormat(video::ECF_A2R10G10B10))
		return video::ECF_A2R10G10B10;
	return video::ECF_A8R8G8B8;
}

video::ECOLOR_FORMAT selectDepthFormat(video::IVideoDriver *driver)
{
	if (driver->queryTextureFormat(video::ECF_D24))
		return video::ECF_D24;
	if (driver->queryTextureFormat(video::ECF_D24S8))
		return video::ECF_D24S8;
	return video::ECF_D16; // fallback depth format
}
