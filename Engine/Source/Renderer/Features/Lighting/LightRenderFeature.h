#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Lighting/LightStats.h"
#include "Renderer/Features/Lighting/LightTypes.h"
#include "Renderer/Resources/Shader/ShaderHandles.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

#include <d3d11.h>

class FRenderer;

enum class ELightingModel : uint8
{
	Gouraud,
	Lambert,
	Phong,
	Toon
};

struct FLightShaderVariantHandles
{
	std::shared_ptr<FVertexShaderHandle> VertexHandle;
	std::shared_ptr<FPixelShaderHandle>  PixelHandle;
};

class ENGINE_API FLightRenderFeature
{
public:
	~FLightRenderFeature();

	bool PrepareClusteredLightResources(
		FRenderer&                 Renderer,
		const FSceneViewData&      SceneViewData,
		const FSceneRenderTargets& Targets);

	bool Render(
		FRenderer&                 Renderer,
		const FSceneViewData&      SceneViewData,
		const FSceneRenderTargets& Targets);

	void Release();

	void SetLightingModel(ELightingModel Model)
	{
		CurrentLightingModel = Model;
	}

	ELightingModel GetLightingModel() const
	{
		return CurrentLightingModel;
	}

	std::shared_ptr<FVertexShaderHandle> GetCurrentVSHandle(bool bHasNormalMap, ERenderMode RenderMode) const;
	std::shared_ptr<FPixelShaderHandle>  GetCurrentPSHandle(bool bHasNormalMap, ERenderMode RenderMode) const;

	FLightStats GetStats() const
	{
		return Stats;
	}

private:
	bool Initialize(FRenderer& Renderer);
	bool CompileShaderVariants(FRenderer& Renderer);

	void UpdateGlobalLightConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	void UpdateClusterGlobalConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	void UploadLocalLightBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureStagingBuffer(FRenderer& Renderer, uint32 ElementStride, uint32 ElementCount, ID3D11Buffer*& Buffer);

	bool EnsureDynamicStructuredBufferSRV(
		FRenderer&                 Renderer,
		uint32                     ElementStride,
		uint32                     ElementCount,
		ID3D11Buffer*&             Buffer,
		ID3D11ShaderResourceView*& SRV);

	bool EnsureDefaultStructuredBufferSRVUAV(
		FRenderer&                  Renderer,
		uint32                      ElementStride,
		uint32                      ElementCount,
		ID3D11Buffer*&              Buffer,
		ID3D11ShaderResourceView*&  SRV,
		ID3D11UnorderedAccessView*& UAV);

	bool EnsureTimingQueries(FRenderer& Renderer);
	void ResolveTimingQueries(ID3D11DeviceContext* DeviceContext);

	static uint32 ToShaderVariantIndex(bool bHasNormalMap)
	{
		return bHasNormalMap ? 1u : 0u;
	}

	static constexpr uint32 ShaderVariantCount     = 2;
	static constexpr uint32 TimingQueryBufferCount = 3;

	struct FLightTimingQuerySet
	{
		ID3D11Query* Disjoint             = nullptr;
		ID3D11Query* TileDepthBoundsStart = nullptr;
		ID3D11Query* TileDepthBoundsEnd   = nullptr;
		ID3D11Query* LightCullingStart    = nullptr;
		ID3D11Query* LightCullingEnd      = nullptr;
		bool         bPending             = false;
	};

	ID3D11Buffer* GlobalLightConstantBuffer   = nullptr;
	ID3D11Buffer* ClusterGlobalConstantBuffer = nullptr;

	ID3D11Buffer*             LocalLightBuffer = nullptr;
	ID3D11ShaderResourceView* LocalLightSRV    = nullptr;

	ID3D11Buffer*             LightCullProxyBuffer = nullptr;
	ID3D11ShaderResourceView* LightCullProxySRV    = nullptr;

	ID3D11Buffer*             ObjectLightIndexBuffer = nullptr;
	ID3D11ShaderResourceView* ObjectLightIndexSRV    = nullptr;

	ID3D11Buffer*              ClusterLightHeaderBuffer = nullptr;
	ID3D11ShaderResourceView*  ClusterLightHeaderSRV    = nullptr;
	ID3D11UnorderedAccessView* ClusterLightHeaderUAV    = nullptr;

	ID3D11Buffer*              ClusterLightIndexBuffer = nullptr;
	ID3D11ShaderResourceView*  ClusterLightIndexSRV    = nullptr;
	ID3D11UnorderedAccessView* ClusterLightIndexUAV    = nullptr;

	ID3D11Buffer*              TileDepthBoundsBuffer = nullptr;
	ID3D11ShaderResourceView*  TileDepthBoundsSRV    = nullptr;
	ID3D11UnorderedAccessView* TileDepthBoundsUAV    = nullptr;

	std::shared_ptr<FComputeShaderHandle> TileDepthBoundsCS = nullptr;
	std::shared_ptr<FComputeShaderHandle> LightCullingCS    = nullptr;
	ID3D11SamplerState*                   DepthSampler      = nullptr;

	FLightShaderVariantHandles GouraudVariants[ShaderVariantCount]     = {};
	FLightShaderVariantHandles LambertVariants[ShaderVariantCount]     = {};
	FLightShaderVariantHandles PhongVariants[ShaderVariantCount]       = {};
	FLightShaderVariantHandles WorldNormalVariants[ShaderVariantCount] = {};
	FLightShaderVariantHandles ToonVariants[ShaderVariantCount]        = {};

	ELightingModel CurrentLightingModel = ELightingModel::Phong;

	ID3D11Buffer*        ClusterHeaderStagingBuffer              = nullptr;
	uint32               PendingReadbackClusterCount             = 0;
	bool                 bHasPendingReadback                     = false;
	FLightTimingQuerySet TimingQuerySets[TimingQueryBufferCount] = {};
	uint32               TimingQueryWriteIndex                   = 0;

	FLightStats Stats;
};
