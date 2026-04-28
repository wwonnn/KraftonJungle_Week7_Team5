#pragma once

#include "CoreMinimal.h"
#include "ShadowTypes.h"
#include <d3d11.h>

#include "Renderer/Resources/Shader/ShaderHandles.h"

struct FSceneViewData;
struct FSceneRenderTargets;
class FMeshPassProcessor;
class FRenderer;
class FShadowAtlasAllocator;

enum class EShadowDebugViewMode : uint32
{
	None        = 0,
	Depth       = 1,
	VSMMean     = 2,
	VSMVariance = 3
};

class FShadowRenderFeature
{
public:
	~FShadowRenderFeature();

	void SetDefaultShadowMapResolution(uint32 Resolution);

	uint32 GetDefaultShadowMapResolution() const
	{
		return DefaultShadowMapResolution;
	}

	void SetGlobalFilterMode(EShadowFilterMode InMode)
	{
		GlobalFilterMode = InMode;
	}

	EShadowFilterMode GetGlobalFilterMode() const
	{
		return GlobalFilterMode;
	}

	void BindShadowResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void UnbindShadowResources(FRenderer& Renderer);

	void ClearShadowAtlasState(FRenderer& Renderer);

	void Release();

	ID3D11ShaderResourceView* GetLocalShadowDepthAtlasSRV() const
	{
		return LocalShadowDepthAtlasSRV;
	}

	ID3D11ShaderResourceView* GetLocalShadowMomentsAtlasSRV() const
	{
		return LocalShadowMomentsAtlasSRV;
	}

	ID3D11ShaderResourceView* GetDirShadowDepthAtlasSRV() const
	{
		return DirShadowDepthAtlasSRV;
	}

	ID3D11ShaderResourceView* GetDirShadowMomentsAtlasSRV() const
	{
		return DirShadowMomentsAtlasSRV;
	}

	ID3D11ShaderResourceView* GetLocalShadowAtlasPreviewSRV() const
	{
		return LocalShadowAtlasPreviewSRV ? LocalShadowAtlasPreviewSRV : LocalShadowDepthAtlasSRV;
	}

	ID3D11ShaderResourceView* GetDirShadowAtlasPreviewSRV() const
	{
		return DirShadowAtlasPreviewSRV ? DirShadowAtlasPreviewSRV : DirShadowDepthAtlasSRV;
	}

	bool RenderShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

	void SetDebugViewMode(EShadowDebugViewMode InMode)
	{
		DebugViewMode = InMode;
	}

	void SetDebugViewSlice(uint32 InSlice)
	{
		DebugViewSlice = InSlice;
	}

	EShadowDebugViewMode GetDebugViewMode() const
	{
		return DebugViewMode;
	}

	uint32 GetDebugViewSlice() const
	{
		return DebugViewSlice;
	}
	void SetDebugDirectional(bool bInDirectional) { bDebugDirectional = bInDirectional; }
	bool IsDebugDirectional() const { return bDebugDirectional; }
	ID3D11ShaderResourceView* GetShadowDebugPreviewSRV() const
	{
		return ShadowDebugPreviewSRV;
	}

	const TArray<uint32>& GetDebugAvailableSlices() const
	{
		return DebugAvailableSlices;
	}

	const TArray<FShadowViewRenderItem>& GetLastDirectionalShadowViews() const
	{
		return CachedDirShadowViews;
	}

	const TArray<FShadowViewRenderItem>& GetLastLocalShadowViews() const
	{
		return CachedLocalShadowViews;
	}

	void SetDebugViewportOverlayEnabled(bool bEnabled)
	{
		bDebugViewportOverlayEnabled = bEnabled;
	}

	bool IsDebugViewportOverlayEnabled() const
	{
		return bDebugViewportOverlayEnabled;
	}

private:
	bool EnsureLinearSampler(const FRenderer& Renderer);
	bool EnsureMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution);

	bool EnsureShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount);

	bool EnsureDirMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureDirShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureDirShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount);

	bool EnsureDynamicStructuredBufferSRV(
		FRenderer&                 Renderer,
		uint32                     ElementStride,
		uint32                     ElementCount,
		ID3D11Buffer*&             Buffer,
		ID3D11ShaderResourceView*& SRV);

	bool EnsureComparisonSampler(FRenderer& Renderer);

	void UploadShadowBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void RenderShadowViews(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);
	void RenderDirectionalShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

	uint32         ResolveShadowViewResolution(uint32 RequestedResolution) const;
	uint32         ComputeRequiredShadowDepthArrayResolution(const FSceneViewData& SceneViewData) const;
	D3D11_VIEWPORT BuildShadowViewport(int X, int Y, int Size) const;
	bool           EnsureDebugPreviewResources(FRenderer& Renderer);
	bool           RenderDebugPreview(FRenderer& Renderer, FSceneRenderTargets& Targets, const FSceneViewData& SceneViewData);
	bool           EnsureAtlasPreviewTexture(
		FRenderer& Renderer,
		uint32 Size,
		ID3D11Texture2D*& Texture,
		ID3D11RenderTargetView*& RTV,
		ID3D11ShaderResourceView*& SRV);
	bool           RenderAtlasPreview(
		FRenderer& Renderer,
		const FSceneViewData& SceneViewData,
		ID3D11ShaderResourceView* SourceSRV,
		uint32 Size,
		ID3D11RenderTargetView* TargetRTV);
	void           RenderShadowAtlasPreviews(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	ID3D11Texture2D*          ShadowDebugPreviewTexture = nullptr;
	ID3D11RenderTargetView*   ShadowDebugPreviewRTV     = nullptr;
	ID3D11ShaderResourceView* ShadowDebugPreviewSRV     = nullptr;

	ID3D11Texture2D*          LocalShadowAtlasPreviewTexture = nullptr;
	ID3D11RenderTargetView*   LocalShadowAtlasPreviewRTV     = nullptr;
	ID3D11ShaderResourceView* LocalShadowAtlasPreviewSRV     = nullptr;

	ID3D11Texture2D*          DirShadowAtlasPreviewTexture = nullptr;
	ID3D11RenderTargetView*   DirShadowAtlasPreviewRTV     = nullptr;
	ID3D11ShaderResourceView* DirShadowAtlasPreviewSRV     = nullptr;

	ID3D11SamplerState* ShadowDebugSampler = nullptr;
	ID3D11Buffer*       ShadowDebugCB      = nullptr;

	std::shared_ptr<FVertexShaderHandle> ShadowDebugVS = nullptr;
	std::shared_ptr<FPixelShaderHandle>  ShadowDebugPS = nullptr;;
	std::shared_ptr<FPixelShaderHandle>  ShadowAtlasPreviewPS = nullptr;

	//Spot
	ID3D11Texture2D*		  LocalShadowDepthAtlas						   = nullptr;
	ID3D11DepthStencilView*   LocalShadowDepthAtlasDSV					   = nullptr;
	ID3D11ShaderResourceView* LocalShadowDepthAtlasSRV					   = nullptr;

	ID3D11Texture2D* LocalShadowMomentsAtlas = nullptr;
	ID3D11RenderTargetView* LocalShadowMomentsAtlasRTV = nullptr;
	ID3D11ShaderResourceView* LocalShadowMomentsAtlasSRV = nullptr;

	// Spot Light Cache
	ID3D11Texture2D*          LocalShadowCacheDepthAtlas                             = nullptr;
	ID3D11ShaderResourceView* LocalShadowCacheDepthAtlasSRV                          = nullptr;
	ID3D11DepthStencilView*   LocalShadowCacheDepthAtlasDSV                          = nullptr;

	ID3D11Texture2D*          LocalShadowCacheMomentsAtlas                           = nullptr;
	ID3D11ShaderResourceView* LocalShadowCacheMomentsAtlasSRV                        = nullptr;
	ID3D11RenderTargetView*   LocalShadowCacheMomentsAtlasRTV                        = nullptr;


	ID3D11Texture2D* ShadowCacheDepthCube = nullptr;
	ID3D11Texture2D* ShadowCacheMomentsCube = nullptr;
	ID3D11ShaderResourceView* ShadowCacheDepthCubeSRV = nullptr;
	ID3D11ShaderResourceView* ShadowCacheMomentsCubeSRV = nullptr;
	ID3D11DepthStencilView*   ShadowCacheDepthCubeDSVs[ShadowConfig::MaxShadowViews]   = {};
	ID3D11RenderTargetView*   ShadowCacheMomentsCubeRTVs[ShadowConfig::MaxShadowViews] = {};
	//PointCubeMap
	ID3D11Texture2D*          ShadowDepthCubeArray                             = nullptr;
	ID3D11ShaderResourceView* ShadowDepthCubeArraySRV					   = nullptr;
	ID3D11DepthStencilView*   ShadowDepthCubeDSVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Texture2D* ShadowMomentsCubeArray = nullptr;
	ID3D11ShaderResourceView* ShadowMomentsCubeArraySRV = nullptr;
	ID3D11RenderTargetView* ShadowMomentsCubeRTVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Buffer*             ShadowLightBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowLightBufferSRV = nullptr;

	ID3D11Buffer*             ShadowViewBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowViewBufferSRV = nullptr;

	//Directional
	ID3D11Texture2D*			DirShadowDepthAtlas		= nullptr;
	ID3D11ShaderResourceView*	DirShadowDepthAtlasSRV	= nullptr;
	ID3D11DepthStencilView*		DirShadowDepthAtlasDSV = nullptr;

	ID3D11Texture2D*			DirShadowMomentsAtlas	= nullptr;
	ID3D11RenderTargetView*		DirShadowMomentsAtlasRTV = nullptr;
	ID3D11ShaderResourceView*	DirShadowMomentsAtlasSRV = nullptr;

	ID3D11Buffer*				DirShadowLightBuffer	= nullptr;
	ID3D11ShaderResourceView*	DirShadowLightBufferSRV	= nullptr;

	ID3D11Buffer*				DirShadowViewBuffer		= nullptr;
	ID3D11ShaderResourceView*	DirShadowViewBufferSRV = nullptr;


	ID3D11SamplerState*  ShadowComparisonSampler    = nullptr;
	ID3D11SamplerState*  ShadowLinearSampler        = nullptr;
	uint32               DefaultShadowMapResolution = ShadowConfig::DefaultShadowMapResolution;
	uint32               ShadowDepthArrayResolution = ShadowConfig::DefaultShadowMapResolution;
	bool                 bShadowDepthArrayDirty = true;
	bool                 bDirShadowDepthArrayDirty     = true;
	bool                 bMomentsBlurValid          = false;
	bool				 bDebugDirectional			= true;
	EShadowFilterMode    GlobalFilterMode           = EShadowFilterMode::VSM;
	EShadowDebugViewMode DebugViewMode              = EShadowDebugViewMode::None;
	uint32               DebugViewSlice             = 0;
	float                DebugVarianceExposure      = 5000.0f;;
	TArray<uint32>       DebugAvailableSlices;
	TArray<FShadowViewRenderItem> CachedLocalShadowViews;
	TArray<FShadowViewRenderItem> CachedDirShadowViews;
	bool                 bDebugViewportOverlayEnabled = false;

	FShadowAtlasAllocator* ShadowAtlasAllocator;
	FShadowAtlasAllocator* DirShadowAtlasAllocator;
};
