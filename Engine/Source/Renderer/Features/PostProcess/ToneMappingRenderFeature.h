#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFeatureInterfaces.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

#include "Renderer/Common/RenderFrameContext.h"

class FRenderer;

class ENGINE_API FToneMappingRenderFeature
{
public:
	~FToneMappingRenderFeature();

	bool Render(
		FRenderer&           Renderer,
		const FFrameContext& Frame,
		const FViewContext&  View,
		FSceneRenderTargets& Targets);

	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateConstantBuffer(FRenderer& Renderer);

	ID3D11Buffer*            ToneMappingConstantBuffer  = nullptr;
	ID3D11RasterizerState*   ToneMappingRasterizerState = nullptr;
	ID3D11DepthStencilState* NoDepthState               = nullptr;
	ID3D11SamplerState*      LinearSampler              = nullptr;
	ID3D11VertexShader*      FullscreenVS               = nullptr;
	ID3D11PixelShader*       ToneMappingPS              = nullptr;
};
