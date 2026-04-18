#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/Features/PostProcess/ToneMappingRenderFeature.h"

bool FToneMappingPass::Execute(FPassContext& Context)
{
	FToneMappingRenderFeature* Feature = Context.Renderer.GetToneMappingRenderFeature();
	if (!Feature)
	{
		return true;
	}

	return Feature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets);
}

bool FFXAAPass::Execute(FPassContext& Context)
{
	if (!Context.SceneViewData.PostProcessInputs.bApplyFXAA)
	{
		return true;
	}

	FFXAARenderFeature* Feature = Context.Renderer.GetFXAAFeature();
	if (!Feature)
	{
		return true;
	}

	return Feature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets);
}
