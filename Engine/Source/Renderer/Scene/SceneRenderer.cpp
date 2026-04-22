#include "Renderer/Scene/SceneRenderer.h"

#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/Pipeline/ScenePipelineBuilder.h"
#include "Renderer/Scene/Pipeline/RenderPipeline.h"
#include "Renderer/Scene/Builders/SceneViewAssembler.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Features/Lighting/LightRenderFeature.h"

FSceneRenderer::FSceneRenderer()
	: SceneCommandBuilder(std::make_unique<FSceneCommandBuilder>())
	  , SceneCommandResourceCache(std::make_unique<FSceneCommandResourceCache>())
	  , MeshPassProcessor(std::make_unique<FMeshPassProcessor>())
{
}

FSceneRenderer::~FSceneRenderer() = default;

void FSceneRenderer::BeginFrame()
{
	PrevCommandCount             = (std::max)(PrevCommandCount, CurrentFramePeakCommandCount);
	CurrentFramePeakCommandCount = 0;
	MeshPassProcessor->BeginFrame();
}

size_t FSceneRenderer::GetPrevCommandCount() const
{
	return (std::max)(PrevCommandCount, CurrentFramePeakCommandCount);
}

const FMeshPassFrameStats& FSceneRenderer::GetMeshPassFrameStats() const
{
	return MeshPassProcessor->GetFrameStats();
}

void FSceneRenderer::BuildSceneViewData(
	FRenderer&                Renderer,
	const FSceneRenderPacket& Packet,
	const FFrameContext&      Frame,
	const FViewContext&       View,
	UWorld*                   World,
	const TArray<FMeshBatch>& AdditionalMeshBatches,
	FSceneViewData&           OutSceneViewData)
{
	BuildSceneViewDataFromPacket(
		Renderer,
		*SceneCommandBuilder,
		*SceneCommandResourceCache,
		Packet,
		Frame,
		View,
		World,
		AdditionalMeshBatches,
		OutSceneViewData);

	CurrentFramePeakCommandCount = (std::max)(CurrentFramePeakCommandCount, OutSceneViewData.MeshInputs.Batches.size());
}

bool FSceneRenderer::RenderSceneView(
	FRenderer&           Renderer,
	FSceneRenderTargets& Targets,
	FSceneViewData&      SceneViewData,
	const float          ClearColor[4],
	bool                 bForceWireframe,
	FMaterial*           WireframeMaterial)
{
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !Targets.IsValid())
	{
		return false;
	}

	switch (SceneViewData.RenderMode)
	{
	case ERenderMode::Lit_Gouraud:
		Renderer.GetLightFeature()->SetLightingModel(ELightingModel::Gouraud);
		break;
	case ERenderMode::Lit_Lambert:
		Renderer.GetLightFeature()->SetLightingModel(ELightingModel::Lambert);
		break;
	case ERenderMode::Lit_Phong:
	case ERenderMode::LightCullingHeatmap:
		Renderer.GetLightFeature()->SetLightingModel(ELightingModel::Phong);
		break;
	case ERenderMode::Lit_Toon:
		Renderer.GetLightFeature()->SetLightingModel(ELightingModel::Toon);
		break;
	default:
		Renderer.GetLightFeature()->SetLightingModel(ELightingModel::Gouraud);
		break;
	}

	if (bForceWireframe)
	{
		ApplyWireframeOverrideToSceneView(SceneViewData, WireframeMaterial);
	}

	FPassContext PassContext
	{
		Renderer,
		Targets,
		SceneViewData,
		FVector4(ClearColor[0], ClearColor[1], ClearColor[2], ClearColor[3])
	};

	// [16-bit HDR] R16G16B16A16_FLOAT 선형 공간에서 모든 씬 패스 실행
	FRenderPipeline Pipeline;
	BuildDefaultSceneRenderPipeline(Pipeline, *MeshPassProcessor);
	if (!Pipeline.Execute(PassContext))
	{
		return false;
	}

	// ACES 톤매핑 + LinearToSRGB 후 [8-bit LDR] R8G8B8A8_UNORM 백버퍼로 blit
	return Renderer.ResolveSceneColorTargets(
		Targets,
		SceneViewData.Frame,
		SceneViewData.View,
		SceneViewData.PostProcessInputs.bApplyFXAA);
}
