#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"

namespace
{
	bool RenderDecalDebugOverlay(FPassContext& Context, ID3D11RenderTargetView* OverlayRenderTarget)
	{
		if (!OverlayRenderTarget || Context.SceneViewData.PostProcessInputs.DecalItems.empty())
		{
			return true;
		}

		const FDecalRenderRequest Request = BuildDecalPassRequest(Context.SceneViewData, EDecalDirtyFlags::None);
		if (!Request.bDebugDraw)
		{
			return true;
		}

		if (Context.Renderer.GetDecalProjectionMode() == EDecalProjectionMode::VolumeDraw)
		{
			FVolumeDecalRenderFeature* VolumeDecalFeature = Context.Renderer.GetVolumeDecalFeature();
			return VolumeDecalFeature
				? VolumeDecalFeature->RenderDebugOverlay(Context.Renderer, Request, Context.Targets, OverlayRenderTarget)
				: true;
		}

		FDecalRenderFeature* DecalFeature = Context.Renderer.GetDecalFeature();
		return DecalFeature
			? DecalFeature->RenderDebugOverlay(
				Context.Renderer,
				Request,
				Context.Targets,
				OverlayRenderTarget,
				FLinearColor::FromSRGB(FVector4(1.0f, 0.6f, 0.1f, 1.0f)))
			: true;
	}

	bool RenderLocalFogDebugOverlay(FPassContext& Context, ID3D11RenderTargetView* OverlayRenderTarget)
	{
		if (!OverlayRenderTarget || Context.SceneViewData.PostProcessInputs.FogItems.empty())
		{
			return true;
		}

		const FDecalRenderRequest Request = BuildLocalFogDebugPassRequest(Context.SceneViewData);
		if (!Request.bDebugDraw || Request.Items.empty())
		{
			return true;
		}

		FDecalRenderFeature* DecalFeature = Context.Renderer.GetDecalFeature();
		return DecalFeature
			? DecalFeature->RenderDebugOverlay(
				Context.Renderer,
				Request,
				Context.Targets,
				OverlayRenderTarget,
				FLinearColor::FromSRGB(FVector4(0.65f, 0.25f, 1.0f, 1.0f)))
			: true;
	}
}

bool FEditorPrimitivePass::Execute(FPassContext& Context)
{
	ID3D11RenderTargetView* OverlayRenderTarget = Context.Targets.OverlayColorRTV
		? Context.Targets.OverlayColorRTV
		: Context.Targets.SceneColorRTV;

	if (!OverlayRenderTarget)
	{
		return true;
	}

	BeginPass(
		Context.Renderer,
		OverlayRenderTarget,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	if (!RenderDecalDebugOverlay(Context, OverlayRenderTarget))
	{
		EndPass(
			Context.Renderer,
			Context.Targets.SceneColorRTV,
			Context.Targets.SceneDepthDSV,
			Context.SceneViewData.View.Viewport,
			Context.SceneViewData.Frame,
			Context.SceneViewData.View);
		return false;
	}
	if (!RenderLocalFogDebugOverlay(Context, OverlayRenderTarget))
	{
		EndPass(
			Context.Renderer,
			Context.Targets.SceneColorRTV,
			Context.Targets.SceneDepthDSV,
			Context.SceneViewData.View.Viewport,
			Context.SceneViewData.Frame,
			Context.SceneViewData.View);
		return false;
	}
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::EditorPrimitive);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FEditorLinePass::Execute(FPassContext& Context)
{
	FDebugLineRenderFeature* DebugLineFeature = Context.Renderer.GetDebugLineFeature();
	if (!DebugLineFeature || Context.SceneViewData.DebugInputs.LinePass.IsEmpty())
	{
		return true;
	}

	return DebugLineFeature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Context.SceneViewData.DebugInputs.LinePass);
}
