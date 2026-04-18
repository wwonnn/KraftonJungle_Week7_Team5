#include "Renderer/Scene/Pipeline/ScenePipelineBuilder.h"

#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePasses.h"
#include "Renderer/Scene/Pipeline/RenderPipeline.h"

#include <memory>

void BuildDefaultSceneRenderPipeline(FRenderPipeline& OutPipeline, const FMeshPassProcessor& MeshPassProcessor)
{
	OutPipeline.Reset();

	// Scene Geometry
	OutPipeline.AddPass(std::make_unique<FClearSceneTargetsPass>());
	OutPipeline.AddPass(std::make_unique<FUploadMeshBuffersPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDepthPrepass>(MeshPassProcessor));
	// Inactive for the current forward-focused renderer. Keep the pass code and shaders, but do not execute it.
	// OutPipeline.AddPass(std::make_unique<FGBufferPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FForwardOpaquePass>(MeshPassProcessor));

	// Scene Effects
	OutPipeline.AddPass(std::make_unique<FDecalCompositePass>());
	OutPipeline.AddPass(std::make_unique<FFogPostPass>());
	OutPipeline.AddPass(std::make_unique<FFireBallPass>());
	OutPipeline.AddPass(std::make_unique<FForwardTransparentPass>(MeshPassProcessor));

	// Editor World Overlay
	OutPipeline.AddPass(std::make_unique<FEditorGridPass>(MeshPassProcessor));

	// Selection Highlight
	OutPipeline.AddPass(std::make_unique<FOutlineMaskPass>());
	OutPipeline.AddPass(std::make_unique<FOutlineCompositePass>());

	// Final Image Post Process
	OutPipeline.AddPass(std::make_unique<FToneMappingPass>());
	OutPipeline.AddPass(std::make_unique<FFXAAPass>());

	// Editor Screen Overlay
	OutPipeline.AddPass(std::make_unique<FEditorLinePass>());
	OutPipeline.AddPass(std::make_unique<FEditorPrimitivePass>(MeshPassProcessor));
}
