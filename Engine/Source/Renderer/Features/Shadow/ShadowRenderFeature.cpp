#include "Renderer/Features/Shadow/ShadowRenderFeature.h"
#include "Renderer/Features/Shadow/ShadowAtlasAllocator.h"
#include "Component/LightComponentBase.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Resources/Material/Material.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "Core/Paths.h"
#include "Math/MathUtility.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

namespace
{
	template <typename T>
	void SafeRelease(T*& Ptr)
	{
		if (Ptr)
		{
			Ptr->Release();
			Ptr = nullptr;
		}
	}

	bool HasShadowVSMCaster(const FSceneViewData& SceneViewData)
	{
		for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
		{
			if (!Batch.Mesh || !Batch.Material)
			{
				continue;
			}
			if (!EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::ShadowVSM))
			{
				continue;
			}
			if (Batch.Material->GetPassShaders(EMaterialPassType::ShadowVSM) == nullptr)
			{
				continue;
			}
			return true;
		}

		return false;
	}

	bool HasSceneRenderBatch(const FSceneViewData& SceneViewData)
	{
		const uint32 EditorOnlyMask =
			static_cast<uint32>(EMeshPassMask::EditorPicking) |
			static_cast<uint32>(EMeshPassMask::EditorGrid) |
			static_cast<uint32>(EMeshPassMask::EditorPrimitive);

		for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
		{
			if ((Batch.PassMask & ~EditorOnlyMask) != 0u)
			{
				return true;
			}
		}

		return false;
	}

	bool ShouldPreserveAtlasForAuxiliaryPass(const FSceneViewData& SceneViewData)
	{
		return HasSceneRenderBatch(SceneViewData);
	}
}

FShadowRenderFeature::~FShadowRenderFeature()
{
	Release();
}

void FShadowRenderFeature::SetDefaultShadowMapResolution(uint32 Resolution)
{
	const uint32 NewResolution = FMath::Clamp(
		Resolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	if (DefaultShadowMapResolution == NewResolution)
	{
		return;
	}

	DefaultShadowMapResolution = NewResolution;
	bShadowDepthArrayDirty     = true;
}

void FShadowRenderFeature::BindShadowResources(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	const bool bNeedLinearSampler = (GlobalFilterMode == EShadowFilterMode::VSM || GlobalFilterMode == EShadowFilterMode::ESM);

	if (ShadowComparisonSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	}

	if (bNeedLinearSampler && ShadowLinearSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
	}
	else
	{
		ID3D11SamplerState* NullSampler = nullptr;
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
	}

	ID3D11ShaderResourceView* MomentsSRV = (bNeedLinearSampler) ? LocalShadowMomentsAtlasSRV : nullptr;
	ID3D11ShaderResourceView* SRVs[4] =
	{
		ShadowLightBufferSRV,
		ShadowViewBufferSRV,
		LocalShadowDepthAtlasSRV,
		MomentsSRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);

	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowCubeSRV, 1, &ShadowDepthCubeArraySRV);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowMomentCubeSRV, 1, &ShadowMomentsCubeArraySRV);

	ID3D11ShaderResourceView* DirMomentsSRV = nullptr;
	if (bNeedLinearSampler)
	{
		DirMomentsSRV = DirShadowMomentsAtlasSRV;
	}

	ID3D11ShaderResourceView* DirSRVs[4] =
	{
		DirShadowLightBufferSRV,
		DirShadowViewBufferSRV,
		DirShadowDepthAtlasSRV,
		DirMomentsSRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, DirSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, DirSRVs);
}

void FShadowRenderFeature::UnbindShadowResources(FRenderer& Renderer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	ID3D11ShaderResourceView* NullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	ID3D11SamplerState*       NullSampler = nullptr;
	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, NullSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, NullSRVs);
	DeviceContext->VSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, NullSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, NullSRVs);
	
	EnsureComparisonSampler(Renderer);
	EnsureLinearSampler(Renderer);

	if (ShadowComparisonSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	}

	if (ShadowLinearSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
	}
}

void FShadowRenderFeature::ClearShadowAtlasState(FRenderer& Renderer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	static const float ClearMoments[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

	if (LocalShadowDepthAtlasDSV)
	{
		DeviceContext->ClearDepthStencilView(LocalShadowDepthAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	}
	if (LocalShadowMomentsAtlasRTV)
	{
		DeviceContext->ClearRenderTargetView(LocalShadowMomentsAtlasRTV, ClearMoments);
	}
	if (DirShadowDepthAtlasDSV)
	{
		DeviceContext->ClearDepthStencilView(DirShadowDepthAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	}
	if (DirShadowMomentsAtlasRTV)
	{
		DeviceContext->ClearRenderTargetView(DirShadowMomentsAtlasRTV, ClearMoments);
	}

	if (ShadowAtlasAllocator)
	{
		ShadowAtlasAllocator->Reset();
	}
	if (DirShadowAtlasAllocator)
	{
		DirShadowAtlasAllocator->Reset();
	}

	CachedLocalShadowViews.clear();
	CachedDirShadowViews.clear();
	DebugAvailableSlices.clear();

	SafeRelease(LocalShadowAtlasPreviewSRV);
	SafeRelease(LocalShadowAtlasPreviewRTV);
	SafeRelease(LocalShadowAtlasPreviewTexture);
	SafeRelease(DirShadowAtlasPreviewSRV);
	SafeRelease(DirShadowAtlasPreviewRTV);
	SafeRelease(DirShadowAtlasPreviewTexture);
}

void FShadowRenderFeature::Release()
{
	SafeRelease(ShadowDebugPreviewSRV);
	SafeRelease(ShadowDebugPreviewRTV);
	SafeRelease(ShadowDebugPreviewTexture);
	SafeRelease(LocalShadowAtlasPreviewSRV);
	SafeRelease(LocalShadowAtlasPreviewRTV);
	SafeRelease(LocalShadowAtlasPreviewTexture);
	SafeRelease(DirShadowAtlasPreviewSRV);
	SafeRelease(DirShadowAtlasPreviewRTV);
	SafeRelease(DirShadowAtlasPreviewTexture);
	SafeRelease(ShadowDebugSampler);
	SafeRelease(ShadowDebugCB);

	ShadowDebugVS.reset();
	ShadowDebugPS.reset();
	ShadowAtlasPreviewPS.reset();

	SafeRelease(ShadowLinearSampler);
	SafeRelease(ShadowComparisonSampler);

	SafeRelease(ShadowViewBufferSRV);
	SafeRelease(ShadowViewBuffer);

	SafeRelease(ShadowLightBufferSRV);
	SafeRelease(ShadowLightBuffer);

	SafeRelease(LocalShadowDepthAtlasSRV);
	SafeRelease(LocalShadowDepthAtlasDSV);
	SafeRelease(LocalShadowDepthAtlas);

	SafeRelease(LocalShadowMomentsAtlasSRV);
	SafeRelease(LocalShadowMomentsAtlasRTV);
	SafeRelease(LocalShadowMomentsAtlas);

	SafeRelease(LocalShadowCacheDepthAtlasSRV);
	SafeRelease(LocalShadowCacheDepthAtlasDSV);
	SafeRelease(LocalShadowCacheDepthAtlas);

	SafeRelease(LocalShadowCacheMomentsAtlasSRV);
	SafeRelease(LocalShadowCacheMomentsAtlasRTV);
	SafeRelease(LocalShadowCacheMomentsAtlas);


	for (ID3D11DepthStencilView*& DSV : ShadowDepthCubeDSVs)
	{
		SafeRelease(DSV);
	}

	SafeRelease(DirShadowDepthAtlas);

	SafeRelease(DirShadowViewBufferSRV);
	SafeRelease(DirShadowViewBuffer);

	SafeRelease(DirShadowLightBufferSRV);
	SafeRelease(DirShadowLightBuffer);


	SafeRelease(DirShadowMomentsAtlasSRV);
	SafeRelease(DirShadowMomentsAtlasRTV);
	SafeRelease(DirShadowDepthAtlasSRV);
	SafeRelease(DirShadowDepthAtlasDSV);
	SafeRelease(DirShadowMomentsAtlas);
	SafeRelease(DirShadowDepthAtlas);

	SafeRelease(ShadowDepthCubeArray);
	SafeRelease(ShadowDepthCubeArraySRV);

	for(ID3D11RenderTargetView*& RTV : ShadowMomentsCubeRTVs)
	{
		SafeRelease(RTV);
	}
	SafeRelease(ShadowMomentsCubeArray);
	SafeRelease(ShadowMomentsCubeArraySRV);

	// Stationary 캐시 자원
	for (ID3D11DepthStencilView*& DSV : ShadowCacheDepthCubeDSVs)
	{
		SafeRelease(DSV);
	}
	for (ID3D11RenderTargetView*& RTV : ShadowCacheMomentsCubeRTVs)
	{
		SafeRelease(RTV);
	}
	SafeRelease(ShadowCacheDepthCubeSRV);
	SafeRelease(ShadowCacheMomentsCubeSRV);
	SafeRelease(ShadowCacheDepthCube);
	SafeRelease(ShadowCacheMomentsCube);

	SafeRelease(PointDebugSRV);
	SafeRelease(PointDebugTexture);

	bMomentsBlurValid      = false;
	bShadowDepthArrayDirty = true;
}

bool FShadowRenderFeature::RenderShadows(
	FRenderer&                Renderer,
	const FMeshPassProcessor& Processor,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	EnsureComparisonSampler(Renderer);
	EnsureLinearSampler(Renderer);

	if ((SceneViewData.LightingInputs.ShadowLights.empty() || SceneViewData.LightingInputs.ShadowViews.empty())
		&& (SceneViewData.LightingInputs.DirShadowLights.empty() || SceneViewData.LightingInputs.DirShadowViews.empty()))
	{
		UnbindShadowResources(Renderer);
		if (!ShouldPreserveAtlasForAuxiliaryPass(SceneViewData))
		{
			ClearShadowAtlasState(Renderer);
		}
		return true;
	}

	UnbindShadowResources(Renderer);

	if (!EnsureResources(Renderer, SceneViewData))
	{
		UnbindShadowResources(Renderer);
		return false;
	}

	RenderShadowViews(Renderer, Processor, Targets, SceneViewData);
	RenderDirectionalShadows(Renderer, Processor, Targets, SceneViewData);
	RenderShadowAtlasPreviews(Renderer, SceneViewData);

	if (DebugViewMode != EShadowDebugViewMode::None)
	{
		RenderDebugPreview(Renderer, Targets, SceneViewData);
	}
	UploadShadowBuffers(Renderer, SceneViewData);
	BindShadowResources(Renderer, SceneViewData);

	return true;
}


ID3D11ShaderResourceView* FShadowRenderFeature::GetPointLightFacePreviewSRV(ID3D11Device* Device, ID3D11DeviceContext* Context, uint32 ArraySlice)
{
	ID3D11ShaderResourceView* CubeArraySRV = ShadowDepthCubeArraySRV;
	if (!CubeArraySRV) return nullptr;

	ID3D11Resource* CubeRes = nullptr;
	CubeArraySRV->GetResource(&CubeRes);

	D3D11_TEXTURE2D_DESC CubeDesc;
	static_cast<ID3D11Texture2D*>(CubeRes)->GetDesc(&CubeDesc);

	if (!PointDebugTexture)
	{
		D3D11_TEXTURE2D_DESC TexDesc = CubeDesc;
		TexDesc.ArraySize = 1;
		TexDesc.MiscFlags = 0;
		TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Device->CreateTexture2D(&TexDesc, nullptr, &PointDebugTexture);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		Device->CreateShaderResourceView(PointDebugTexture, &srvDesc, &PointDebugSRV);
	}

	uint32 Subresource = D3D11CalcSubresource(0, ArraySlice, CubeDesc.MipLevels);
	Context->CopySubresourceRegion(PointDebugTexture, 0, 0, 0, 0, CubeRes, Subresource, nullptr);

	CubeRes->Release();

	return PointDebugSRV;
}

bool FShadowRenderFeature::EnsureLinearSampler(const FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (ShadowLinearSampler)
	{
		return true;
	}

	D3D11_SAMPLER_DESC Desc = {};
	Desc.Filter             = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	Desc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.MinLOD             = 0.0f;
	Desc.MaxLOD             = D3D11_FLOAT32_MAX;

	return SUCCEEDED(Device->CreateSamplerState(&Desc, &ShadowLinearSampler)) && ShadowLinearSampler;
}

bool FShadowRenderFeature::EnsureMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	RequiredResolution = FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	if (LocalShadowMomentsAtlas)
	{
		return true;
	}

	/////////////////////////////////////////////////////////////////////
	// Atlas
	/////////////////////////////////////////////////////////////////////
	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowConfig::MaxShadowMapResolution;
	TextureDesc.Height               = ShadowConfig::MaxShadowMapResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.Format               = DXGI_FORMAT_R32G32_FLOAT;
	TextureDesc.ArraySize = 1;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &LocalShadowMomentsAtlas)) || !LocalShadowMomentsAtlas)
	{
		SafeRelease(LocalShadowMomentsAtlas);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = TextureDesc.Format;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(LocalShadowMomentsAtlas, &SRVDesc, &LocalShadowMomentsAtlasSRV)) || !LocalShadowMomentsAtlasSRV)
	{
		SafeRelease(LocalShadowMomentsAtlas);
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc  = {};
	RTVDesc.Format                         = DXGI_FORMAT_R32G32_FLOAT;
	RTVDesc.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Texture2D.MipSlice = 0;

	if (FAILED(Device->CreateRenderTargetView(LocalShadowMomentsAtlas, &RTVDesc, &LocalShadowMomentsAtlasRTV)) || !LocalShadowMomentsAtlasRTV)
	{
		return false;
	}

	////////////////////////////////////////////////////////////////////
	// Cube array (for point light shadows)
	/////////////////////////////////////////////////////////////////////

	D3D11_TEXTURE2D_DESC CubeTextureDesc = {};
	CubeTextureDesc.Width = ShadowDepthArrayResolution;   // 깊이 큐브와 동일 크기
	CubeTextureDesc.Height = ShadowDepthArrayResolution;
	CubeTextureDesc.MipLevels = 1;
	CubeTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	CubeTextureDesc.ArraySize = ShadowConfig::MaxShadowViews;
	CubeTextureDesc.SampleDesc.Count = 1;
	CubeTextureDesc.SampleDesc.Quality = 0;
	CubeTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	CubeTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	CubeTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (FAILED(Device->CreateTexture2D(&CubeTextureDesc, nullptr, &ShadowMomentsCubeArray)) || !ShadowMomentsCubeArray)
	{
		SafeRelease(ShadowMomentsCubeArray);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC CubeSRVDesc = {};
	CubeSRVDesc.Format = CubeTextureDesc.Format;
	CubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	CubeSRVDesc.Texture2D.MostDetailedMip = 0;
	CubeSRVDesc.Texture2D.MipLevels = 1;
	CubeSRVDesc.TextureCubeArray.First2DArrayFace = ShadowConfig::PointShadowSliceOffset;
	CubeSRVDesc.TextureCubeArray.NumCubes = ShadowConfig::MaxPointShadowCubes;

	if (FAILED(Device->CreateShaderResourceView(ShadowMomentsCubeArray, &CubeSRVDesc, &ShadowMomentsCubeArraySRV)) || !ShadowMomentsCubeArraySRV)
	{
		SafeRelease(ShadowMomentsCubeArraySRV);
		SafeRelease(ShadowMomentsCubeArray);
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_RENDER_TARGET_VIEW_DESC CubeRTVDesc = {};
		CubeRTVDesc.Format = CubeTextureDesc.Format;
		CubeRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		CubeRTVDesc.Texture2DArray.MipSlice = 0;
		CubeRTVDesc.Texture2DArray.FirstArraySlice = Slice;
		CubeRTVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateRenderTargetView(ShadowMomentsCubeArray, &CubeRTVDesc, &ShadowMomentsCubeRTVs[Slice])) || !ShadowMomentsCubeRTVs[Slice])
		{
			// 에러 발생 시 릴리즈 로직 (팀원분들의 기존 스타일 유지)
			SafeRelease(ShadowMomentsCubeArraySRV);
			for (ID3D11RenderTargetView*& RTV : ShadowMomentsCubeRTVs)
			{
				SafeRelease(RTV);
			}
			SafeRelease(ShadowMomentsCubeArray);
			bShadowDepthArrayDirty = true;
			return false;
		}
	}

	////////////////////////////////////////////////////////////////////
	// Stationary Chche only cube texture(Depth + Moments)


	// Cache Depth Cube
	D3D11_TEXTURE2D_DESC CacheDepthDesc = {};
	CacheDepthDesc.Width              = ShadowDepthArrayResolution;
	CacheDepthDesc.Height             = ShadowDepthArrayResolution;
	CacheDepthDesc.MipLevels          = 1;
	CacheDepthDesc.ArraySize          = ShadowConfig::MaxShadowViews;
	CacheDepthDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
	CacheDepthDesc.SampleDesc.Count   = 1;
	CacheDepthDesc.Usage              = D3D11_USAGE_DEFAULT;
	CacheDepthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	CacheDepthDesc.MiscFlags          = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (FAILED(Device->CreateTexture2D(&CacheDepthDesc, nullptr, &ShadowCacheDepthCube)) || !ShadowCacheDepthCube)
	{
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice        = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = Slice;
		DSVDesc.Texture2DArray.ArraySize       = 1;

		if (FAILED(Device->CreateDepthStencilView(ShadowCacheDepthCube, &DSVDesc, &ShadowCacheDepthCubeDSVs[Slice])))
		{
			return false;
		}
	}

	// Cache Moments Cube — 모멘트 큐브와 동일 디스크립션 재사용
	D3D11_TEXTURE2D_DESC CacheMomentsDesc = CubeTextureDesc;

	if (FAILED(Device->CreateTexture2D(&CacheMomentsDesc, nullptr, &ShadowCacheMomentsCube)) || !ShadowCacheMomentsCube)
	{
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc       = {};
		RTVDesc.Format                              = CacheMomentsDesc.Format;
		RTVDesc.ViewDimension                       = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		RTVDesc.Texture2DArray.MipSlice             = 0;
		RTVDesc.Texture2DArray.FirstArraySlice      = Slice;
		RTVDesc.Texture2DArray.ArraySize            = 1;

		if (FAILED(Device->CreateRenderTargetView(ShadowCacheMomentsCube, &RTVDesc, &ShadowCacheMomentsCubeRTVs[Slice])))
		{
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureResources(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	const bool bNeedLinearSampler = (GlobalFilterMode == EShadowFilterMode::VSM || GlobalFilterMode == EShadowFilterMode::ESM);

	bool bGlobalOk = EnsureComparisonSampler(Renderer);
	if (bNeedLinearSampler)
	{
		bGlobalOk = bGlobalOk && EnsureLinearSampler(Renderer);
	}

	if (!bGlobalOk)
	{
		return false;
	}

	const uint32 ShadowLightCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowLights.size()),
		ShadowConfig::MaxShadowLights);

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	const uint32 RequiredResolution = ComputeRequiredShadowDepthArrayResolution(SceneViewData);

	bool bLocalOk = true;
	if (ShadowViewCount > 0)
	{
		bLocalOk = EnsureShadowDepthAtlas(Renderer, RequiredResolution) &&
			EnsureShadowBuffers(Renderer, ShadowLightCount, ShadowViewCount);

		if (bNeedLinearSampler && bLocalOk)
		{
			bLocalOk = EnsureMomentsAtlas(Renderer, RequiredResolution);
		}

		if(GlobalFilterMode == EShadowFilterMode::ESM && bLocalOk)
		{
			bLocalOk = EnsureESMConstantBuffer(Renderer);
		}
	}

	const uint32 DirLightCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.DirShadowLights.size()),
		ShadowConfig::MaxShadowLights);

	const uint32 DirViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()),
		ShadowConfig::MaxDirCascade);

	bool bDirOk = true;
	if (DirViewCount > 0)
	{
		bDirOk = EnsureDirShadowDepthAtlas(Renderer, ShadowConfig::DirMaxShadowDepthResolution) &&
			EnsureDirShadowBuffers(Renderer, DirLightCount, DirViewCount);

		if (bNeedLinearSampler && bDirOk)
		{
			bDirOk = EnsureDirMomentsAtlas(Renderer, ShadowConfig::DirMaxShadowDepthResolution);
		}
	}

	return bLocalOk || bDirOk;
}

bool FShadowRenderFeature::EnsureShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	RequiredResolution = FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	if (LocalShadowDepthAtlas)
	{
		return true;
	}


	/////////////////////////////////////////////////////////////////////
	// Atlas
	/////////////////////////////////////////////////////////////////////

	FShadowAtlasAllocatorDesc Desc;
	Desc.AtlasSize = 4096;
	Desc.MinAllocateSize = 128;
	Desc.MaxFallbackMipDrop = 1;

	ShadowAtlasAllocator = new FShadowAtlasAllocator(Desc);

	D3D11_TEXTURE2D_DESC AtlasTextureDesc = {};
	AtlasTextureDesc.Width = ShadowConfig::MaxShadowMapResolution;
	AtlasTextureDesc.Height = ShadowConfig::MaxShadowMapResolution;
	AtlasTextureDesc.MipLevels = 1;
	AtlasTextureDesc.ArraySize = 1;
	AtlasTextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	AtlasTextureDesc.SampleDesc.Count = 1;
	AtlasTextureDesc.SampleDesc.Quality = 0;
	AtlasTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	AtlasTextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	AtlasTextureDesc.CPUAccessFlags = 0;
	AtlasTextureDesc.MiscFlags = 0;

	if (FAILED(Device->CreateTexture2D(&AtlasTextureDesc, nullptr, &LocalShadowDepthAtlas)) || !LocalShadowDepthAtlas)
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC AtlasSRVDesc = {};
	AtlasSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	AtlasSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	AtlasSRVDesc.Texture2D.MostDetailedMip = 0;
	AtlasSRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(LocalShadowDepthAtlas, &AtlasSRVDesc, &LocalShadowDepthAtlasSRV)) || !LocalShadowDepthAtlasSRV)
	{
		SafeRelease(LocalShadowDepthAtlas);
		return false;
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC AtlasDSVDesc = {};
	AtlasDSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	AtlasDSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	AtlasDSVDesc.Texture2D.MipSlice = 0;
	AtlasDSVDesc.Flags = 0;

	if (FAILED(Device->CreateDepthStencilView(LocalShadowDepthAtlas, &AtlasDSVDesc, &LocalShadowDepthAtlasDSV)) || !LocalShadowDepthAtlasDSV)
	{
		SafeRelease(LocalShadowDepthAtlasSRV);
		SafeRelease(LocalShadowDepthAtlas);
		return false;
	}

	if (FAILED(Device->CreateTexture2D(&AtlasTextureDesc, nullptr, &LocalShadowCacheDepthAtlas)) || !LocalShadowCacheDepthAtlas)
	{
		return false;
	}
	if (FAILED(Device->CreateShaderResourceView(LocalShadowCacheDepthAtlas, &AtlasSRVDesc, &LocalShadowCacheDepthAtlasSRV)) || !LocalShadowCacheDepthAtlasSRV)
	{
		SafeRelease(LocalShadowCacheDepthAtlas);
		return false;
	}
	if (FAILED(Device->CreateDepthStencilView(LocalShadowCacheDepthAtlas, &AtlasDSVDesc, &LocalShadowCacheDepthAtlasDSV)) || !LocalShadowCacheDepthAtlasDSV)
	{
		SafeRelease(LocalShadowCacheDepthAtlasSRV);
		SafeRelease(LocalShadowCacheDepthAtlas);
		return false;
	}


	/////////////////////////////////////////////////////////////////////
	// CubeArray Resources (포인트용) — 슬라이스 [PointShadowSliceOffset, ...) 영역
	/////////////////////////////////////////////////////////////////////
	ShadowDepthArrayResolution = RequiredResolution;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowDepthArrayResolution;
	TextureDesc.Height               = ShadowDepthArrayResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.ArraySize            = ShadowConfig::MaxShadowViews;
	TextureDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.MiscFlags            = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowDepthCubeArray)) || !ShadowDepthCubeArray)
	{
		bShadowDepthArrayDirty = true;
		return false;
	}


	D3D11_SHADER_RESOURCE_VIEW_DESC CubeSRVDesc      = {};
	CubeSRVDesc.Format                               = DXGI_FORMAT_R32_FLOAT;
	CubeSRVDesc.ViewDimension                        = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	CubeSRVDesc.TextureCubeArray.MostDetailedMip     = 0;
	CubeSRVDesc.TextureCubeArray.MipLevels           = 1;
	CubeSRVDesc.TextureCubeArray.First2DArrayFace    = ShadowConfig::PointShadowSliceOffset;
	CubeSRVDesc.TextureCubeArray.NumCubes            = ShadowConfig::MaxPointShadowCubes;

	if (FAILED(Device->CreateShaderResourceView(ShadowDepthCubeArray, &CubeSRVDesc, &ShadowDepthCubeArraySRV)))
	{
		SafeRelease(ShadowDepthCubeArraySRV);
		SafeRelease(ShadowDepthCubeArray);
		bShadowDepthArrayDirty = true;
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = Slice;
		DSVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateDepthStencilView(ShadowDepthCubeArray, &DSVDesc, &ShadowDepthCubeDSVs[Slice])) || !ShadowDepthCubeDSVs[Slice])
		{
			SafeRelease(ShadowDepthCubeArraySRV);

			for (ID3D11DepthStencilView*& DSV : ShadowDepthCubeDSVs)
			{
				SafeRelease(DSV);
			}

			SafeRelease(ShadowDepthCubeArray);
			bShadowDepthArrayDirty = true;
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureShadowBuffers(
	FRenderer& Renderer,
	uint32     ShadowLightCount,
	uint32     ShadowViewCount)
{
	return EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FShadowLightGPU),
		ShadowLightCount,
		ShadowLightBuffer,
		ShadowLightBufferSRV)
		&& EnsureDynamicStructuredBufferSRV(
			Renderer,
			sizeof(FShadowViewGPU),
			ShadowViewCount,
			ShadowViewBuffer,
			ShadowViewBufferSRV);
}

bool FShadowRenderFeature::EnsureESMConstantBuffer(FRenderer& Renderer)
{
	if (ShadowESMConstantBuffer)
	{
		return true;
	}

	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	struct FESMConstantData
	{
		float Exponent;
		float Pad[3];
	};

	D3D11_BUFFER_DESC Desc = {};
	Desc.ByteWidth = sizeof(FESMConstantData);
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &ShadowESMConstantBuffer)) && ShadowESMConstantBuffer;
	return false;
}

void FShadowRenderFeature::UpdateESMConstantBuffer(ID3D11DeviceContext* DeviceContext, float ESMExponent)
{
	struct FShadowESMConstants
	{
		float ESMExponent;
		float Pad[3];
	} ESMConstants;
	ESMConstants.ESMExponent = ESMExponent;
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(ShadowESMConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		std::memcpy(Mapped.pData, &ESMConstants, sizeof(ESMConstants));
		DeviceContext->Unmap(ShadowESMConstantBuffer, 0);
	}
	DeviceContext->PSSetConstantBuffers(9, 1, &ShadowESMConstantBuffer);
}

bool FShadowRenderFeature::EnsureDirMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	RequiredResolution = FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::DirMaxShadowDepthResolution);

	if (DirShadowMomentsAtlasSRV)
	{
		return true;
	}

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = RequiredResolution;
	TextureDesc.Height = RequiredResolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &DirShadowMomentsAtlas)) || !DirShadowMomentsAtlas)
	{
		SafeRelease(DirShadowMomentsAtlas);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = TextureDesc.Format;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(DirShadowMomentsAtlas, &SRVDesc, &DirShadowMomentsAtlasSRV)) || !DirShadowMomentsAtlasSRV)
	{
		SafeRelease(DirShadowMomentsAtlasSRV);
		SafeRelease(DirShadowMomentsAtlas);
		return false;
	}

		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
		RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		RTVDesc.Texture2D.MipSlice = 0;

		if (FAILED(Device->CreateRenderTargetView(DirShadowMomentsAtlas, &RTVDesc, &DirShadowMomentsAtlasRTV)) || !DirShadowMomentsAtlasRTV)
		{
			SafeRelease(DirShadowMomentsAtlasSRV);
			SafeRelease(DirShadowMomentsAtlasRTV);
			SafeRelease(DirShadowMomentsAtlas);

			return false;
		}


	return true;
}

bool FShadowRenderFeature::EnsureDirShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device) return false;

	if (DirShadowDepthAtlasSRV)
	{
		return true;
	}

	FShadowAtlasAllocatorDesc Desc;
	Desc.AtlasSize = 8192;
	Desc.MinAllocateSize = 128;
	Desc.MaxFallbackMipDrop = 1;

	DirShadowAtlasAllocator = new FShadowAtlasAllocator(Desc);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = RequiredResolution;
	TextureDesc.Height = RequiredResolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &DirShadowDepthAtlas)) || !DirShadowDepthAtlas)
	{
		SafeRelease(DirShadowDepthAtlas);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(DirShadowDepthAtlas, &SRVDesc, &DirShadowDepthAtlasSRV)) || !DirShadowDepthAtlasSRV)
	{
		SafeRelease(DirShadowDepthAtlasSRV);
		SafeRelease(DirShadowDepthAtlas);
		return false;
	}


	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;

	if (FAILED(Device->CreateDepthStencilView(DirShadowDepthAtlas, &DSVDesc, &DirShadowDepthAtlasDSV)) || !DirShadowDepthAtlasDSV)
	{
		SafeRelease(DirShadowDepthAtlasSRV);
		SafeRelease(DirShadowDepthAtlasDSV);
		SafeRelease(DirShadowDepthAtlas);
		return false;
	}

	return true;
}

bool FShadowRenderFeature::EnsureDirShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount)
{
	return EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FShadowLightGPU),
		ShadowLightCount,
		DirShadowLightBuffer,
		DirShadowLightBufferSRV)
		&& EnsureDynamicStructuredBufferSRV(
			Renderer,
			sizeof(FShadowViewGPU),
			ShadowViewCount,
			DirShadowViewBuffer,
			DirShadowViewBufferSRV);
}

bool FShadowRenderFeature::EnsureDynamicStructuredBufferSRV(
	FRenderer&                 Renderer,
	uint32                     ElementStride,
	uint32                     ElementCount,
	ID3D11Buffer*&             Buffer,
	ID3D11ShaderResourceView*& SRV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device || ElementStride == 0)
	{
		return false;
	}

	const uint32 SafeElementCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth        = ElementStride * SafeElementCount;

	bool bRecreate = !Buffer || !SRV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC ExistingDesc = {};
		Buffer->GetDesc(&ExistingDesc);

		if (ExistingDesc.ByteWidth < ByteWidth || ExistingDesc.StructureByteStride != ElementStride)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(SRV);
	SafeRelease(Buffer);

	D3D11_BUFFER_DESC BufferDesc   = {};
	BufferDesc.ByteWidth           = ByteWidth;
	BufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
	BufferDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
	BufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	BufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	BufferDesc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &Buffer)) || !Buffer)
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeElementCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)) || !SRV)
	{
		SafeRelease(Buffer);
		return false;
	}

	return true;
}

bool FShadowRenderFeature::EnsureComparisonSampler(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (ShadowComparisonSampler)
	{
		return true;
	}

	D3D11_SAMPLER_DESC Desc = {};
	Desc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	Desc.AddressU           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.AddressV           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.AddressW           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.BorderColor[0]     = 1.0f;
	Desc.BorderColor[1]     = 1.0f;
	Desc.BorderColor[2]     = 1.0f;
	Desc.BorderColor[3]     = 1.0f;
	Desc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
	Desc.MinLOD             = 0.0f;
	Desc.MaxLOD             = D3D11_FLOAT32_MAX;

	return SUCCEEDED(Device->CreateSamplerState(&Desc, &ShadowComparisonSampler)) && ShadowComparisonSampler;
}

void FShadowRenderFeature::UploadShadowBuffers(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	if (ShadowLightBuffer && ShadowViewBuffer)
	{
		
		const uint32 ShadowLightCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.ShadowLights.size()),
			ShadowConfig::MaxShadowLights);

		const uint32 ShadowViewCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
			ShadowConfig::MaxShadowViews);

		{
			std::vector<FShadowLightGPU> GPUData;
			GPUData.resize((std::max)(1u, ShadowLightCount));

			for (uint32 Index = 0; Index < ShadowLightCount; ++Index)
			{
				const FShadowLightRenderItem& Src = SceneViewData.LightingInputs.ShadowLights[Index];

				FShadowLightGPU& Dst = GPUData[Index];
				Dst.LightType = static_cast<uint32>(Src.LightType);
				Dst.FirstViewIndex = Src.FirstViewIndex;
				Dst.ViewCount = Src.ViewCount;
				Dst.Flags = 0;
				Dst.PositionType = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, 0.0f);
				Dst.DirectionBias = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, Src.Bias);
				const float CubeIndexAsFloat = (Src.CubeArrayIndex == UINT32_MAX)
					? 0.0f
					: static_cast<float>(Src.CubeArrayIndex);
				Dst.Params0 = FVector4(Src.SlopeBias, Src.NormalBias, Src.Sharpen, CubeIndexAsFloat);
				Dst.Params1 = FVector4(Src.ESMExponent, 0.0f, 0.0f, 0.0f);
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(ShadowLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, GPUData.data(), sizeof(FShadowLightGPU) * GPUData.size());
				DeviceContext->Unmap(ShadowLightBuffer, 0);
			}
		}

		{
			std::vector<FShadowViewGPU> GPUData;
			GPUData.resize((std::max)(1u, ShadowViewCount));

			for (uint32 Index = 0; Index < ShadowViewCount; ++Index)
			{
				const FShadowViewRenderItem& Src = SceneViewData.LightingInputs.ShadowViews[Index];

				const float AtlasScale = ShadowConfig::MaxShadowMapResolution;
				const float TexelSize = AtlasScale > 0
					? 1.0f / static_cast<float>(AtlasScale)
					: 1.0f;

				FShadowViewGPU& Dst = GPUData[Index];
				Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
				Dst.ArraySlice = Src.ArraySlice;
				Dst.ProjectionType = static_cast<uint32>(Src.ProjectionType);
				Dst.FilterMode = static_cast<uint32>(GlobalFilterMode);
				Dst.Pad0 = 0;
				Dst.ViewParams = FVector4(Src.NearZ, Src.FarZ, AtlasScale, TexelSize);
				Dst.BiasParams = Src.BiasParams;
				Dst.AtlasUV = Src.AtlasUV;
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(ShadowViewBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, GPUData.data(), sizeof(FShadowViewGPU) * GPUData.size());
				DeviceContext->Unmap(ShadowViewBuffer, 0);
			}
		}
	}
	

	if (DirShadowLightBuffer && DirShadowViewBuffer)
	{
		const uint32 DirLightCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.DirShadowLights.size()),
			ShadowConfig::MaxDirCascade);

		const uint32 DirViewCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()),
			ShadowConfig::MaxDirCascade);

		{
			std::vector<FShadowLightGPU> DirGPUData;
			DirGPUData.resize((std::max)(1u, DirLightCount));

			for (uint32 Index = 0; Index < DirLightCount; ++Index)
			{
				const FShadowLightRenderItem& Src = SceneViewData.LightingInputs.DirShadowLights[Index];
				FShadowLightGPU& Dst = DirGPUData[Index];

				Dst.LightType = static_cast<uint32>(Src.LightType);
				Dst.FirstViewIndex = Src.FirstViewIndex;
				Dst.ViewCount = Src.ViewCount;
				Dst.Flags = 0;
				Dst.PositionType = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, 0.0f);
				Dst.DirectionBias = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, Src.Bias);
				Dst.Params0 = FVector4(Src.SlopeBias, Src.NormalBias, Src.Sharpen, 0.0f);
				Dst.Params1 = FVector4(Src.ESMExponent, 0.0f, 0.0f, 0.0f);
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(DirShadowLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, DirGPUData.data(), sizeof(FShadowLightGPU) * DirGPUData.size());
				DeviceContext->Unmap(DirShadowLightBuffer, 0);
			}
		}

		{
			std::vector<FShadowViewGPU> DirGPUData;
			DirGPUData.resize((std::max)(1u, DirViewCount));

			for (uint32 Index = 0; Index < DirViewCount; ++Index)
			{
				const FShadowViewRenderItem& Src = SceneViewData.LightingInputs.DirShadowViews[Index];

				const float AtlasScale = ShadowConfig::DirMaxShadowDepthResolution;
				const float TexelSize = 1.0f / static_cast<float>(ShadowConfig::DirMaxShadowDepthResolution);

				FShadowViewGPU& Dst = DirGPUData[Index];
				Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
				Dst.ArraySlice = Src.ArraySlice;
				Dst.ProjectionType = static_cast<uint32>(Src.ProjectionType);
				Dst.FilterMode = static_cast<uint32>(GlobalFilterMode);
				Dst.Pad0 = 0;
				Dst.ViewParams = FVector4(Src.NearZ, Src.FarZ, AtlasScale, TexelSize);
				Dst.BiasParams = Src.BiasParams;
				Dst.AtlasUV = Src.AtlasUV;
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(DirShadowViewBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, DirGPUData.data(), sizeof(FShadowViewGPU) * DirGPUData.size());
				DeviceContext->Unmap(DirShadowViewBuffer, 0);
			}
		}
	}
}

void FShadowRenderFeature::RenderShadowViews(
	FRenderer&                Renderer,
	const FMeshPassProcessor& Processor,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	if (!LocalShadowDepthAtlasSRV)
	{
		return;
	}

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	if (ShadowViewCount == 0)
	{
		return;
	}

	if (!HasShadowVSMCaster(SceneViewData))
	{
		if (!ShouldPreserveAtlasForAuxiliaryPass(SceneViewData))
		{
			ClearShadowAtlasState(Renderer);
		}
		return;
	}

	const FViewContext OriginalView    = SceneViewData.View;
	static const float ClearMoments[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	DeviceContext->ClearDepthStencilView(LocalShadowDepthAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	DeviceContext->ClearRenderTargetView(LocalShadowMomentsAtlasRTV, ClearMoments);

	ShadowAtlasAllocator->Reset();

	std::vector<uint32> RenderOrder;
	RenderOrder.reserve(ShadowViewCount);
	for (uint32 i = 0; i < ShadowViewCount; ++i)
	{
		RenderOrder.push_back(i);
	}

	std::sort(
		RenderOrder.begin(),
		RenderOrder.end(),
		[&](uint32 A, uint32 B)
		{
			const FShadowViewRenderItem& VA = SceneViewData.LightingInputs.ShadowViews[A];
			const FShadowViewRenderItem& VB = SceneViewData.LightingInputs.ShadowViews[B];
			const bool AIsSpot = VA.LightType == EShadowLightType::Spot;
			const bool BIsSpot = VB.LightType == EShadowLightType::Spot;
			if (AIsSpot != BIsSpot) return AIsSpot; // spot 먼저
			if (AIsSpot && BIsSpot) return VA.RequestedResolution > VB.RequestedResolution;
			return A < B; // point는 기존 순서 유지
		});

	// Spot Light 아틀라스 할당
	for (uint32 ViewIndex : RenderOrder)
	{
		FShadowViewRenderItem& ShadowView = SceneViewData.LightingInputs.ShadowViews[ViewIndex];
		if (ShadowView.LightType == EShadowLightType::Spot)
		{
			const uint32 ResolvedResolution = ResolveShadowViewResolution(ShadowView.RequestedResolution);
			FShadowAtlasAllocation OutAllocation;
			if (ShadowAtlasAllocator->Allocate(ResolvedResolution, OutAllocation))
			{
				ShadowView.AtlasUV = FVector(static_cast<float>(OutAllocation.X), static_cast<float>(OutAllocation.Y), static_cast<float>(OutAllocation.Size));
				ShadowView.AllocatedResolution = OutAllocation.Size;
				ShadowView.bAtlasAllocated = true;

			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// 단계 A — Stationary 라이트의 캐시 갱신 패스 (필요 시에만)
	////////////////////////////////////////////////////////////////////
	for (uint32 LightIdx = 0; LightIdx < SceneViewData.LightingInputs.ShadowLights.size(); ++LightIdx)
	{
		FShadowLightRenderItem& Light = SceneViewData.LightingInputs.ShadowLights[LightIdx];
		if (Light.Mobility == ELightMobility::Movable) continue;

		if (Light.LightType == EShadowLightType::Point)
		{
			if (!Light.bCacheDirty) continue;
			if (Light.ViewCount != 6) continue;

			for (uint32 F = 0; F < 6; ++F)
			{
				const uint32 ViewIdx = Light.FirstViewIndex + F;
				if (ViewIdx >= SceneViewData.LightingInputs.ShadowViews.size()) break;
				const FShadowViewRenderItem& CacheView = SceneViewData.LightingInputs.ShadowViews[ViewIdx];
				const uint32 Slice = CacheView.ArraySlice;

				ID3D11RenderTargetView* CacheRTV = ShadowCacheMomentsCubeRTVs[Slice];
				ID3D11DepthStencilView* CacheDSV = ShadowCacheDepthCubeDSVs[Slice];

				DeviceContext->ClearRenderTargetView(CacheRTV, ClearMoments);
				DeviceContext->ClearDepthStencilView(CacheDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

				SceneViewData.View.View                  = CacheView.View;
				SceneViewData.View.Projection            = CacheView.Projection;
				SceneViewData.View.ViewProjection        = CacheView.ViewProjection;
				SceneViewData.View.InverseView           = CacheView.View.GetInverse();
				SceneViewData.View.InverseProjection     = CacheView.Projection.GetInverse();
				SceneViewData.View.InverseViewProjection = CacheView.ViewProjection.GetInverse();
				SceneViewData.View.CameraPosition        = CacheView.PositionWS;
				SceneViewData.View.NearZ                 = CacheView.NearZ;
				SceneViewData.View.FarZ                  = CacheView.FarZ;
				SceneViewData.View.bOrthographic         = false;
				SceneViewData.View.Viewport              = BuildShadowViewport(0, 0, ShadowDepthArrayResolution);
				SceneViewData.View.bShadowStaticOnly     = true;
				SceneViewData.View.bShadowDynamicOnly    = false;

				BeginPass(Renderer, CacheRTV, CacheDSV, SceneViewData.View.Viewport, SceneViewData.Frame, SceneViewData.View);
				Processor.ExecutePass(Renderer, Targets, SceneViewData, EMeshPassType::ShadowVSM);
			}

			Light.bCacheDirty = false;
			
		}
		else if (Light.LightType == EShadowLightType::Spot)
		{
			if (Light.ViewCount != 1) continue;
			const uint32 ViewIdx = Light.FirstViewIndex;
			if (ViewIdx >= SceneViewData.LightingInputs.ShadowViews.size()) continue;
			const FShadowViewRenderItem& CacheView = SceneViewData.LightingInputs.ShadowViews[ViewIdx];
			
			if (!CacheView.bAtlasAllocated) continue;

			int X = static_cast<int>(CacheView.AtlasUV.X);
			int Y = static_cast<int>(CacheView.AtlasUV.Y);
			int Size = CacheView.AllocatedResolution;

			bool bNeedsUpdate = Light.bCacheDirty;

			if (bNeedsUpdate)
			{
				D3D11_BOX Box;
				Box.left = X; Box.right = X + Size;
				Box.top = Y; Box.bottom = Y + Size;
				Box.front = 0; Box.back = 1;

				// 클리어된 메인 아틀라스를 이용해 캐시 아틀라스의 해당 Rect 초기화
				if (LocalShadowCacheDepthAtlas)
				{
					DeviceContext->CopySubresourceRegion(LocalShadowCacheDepthAtlas, 0, X, Y, 0, LocalShadowDepthAtlas, 0, &Box);
				}
				if (LocalShadowCacheMomentsAtlas && LocalShadowMomentsAtlas)
				{
					DeviceContext->CopySubresourceRegion(LocalShadowCacheMomentsAtlas, 0, X, Y, 0, LocalShadowMomentsAtlas, 0, &Box);
				}

				SceneViewData.View.View                  = CacheView.View;
				SceneViewData.View.Projection            = CacheView.Projection;
				SceneViewData.View.ViewProjection        = CacheView.ViewProjection;
				SceneViewData.View.InverseView           = CacheView.View.GetInverse();
				SceneViewData.View.InverseProjection     = CacheView.Projection.GetInverse();
				SceneViewData.View.InverseViewProjection = CacheView.ViewProjection.GetInverse();
				SceneViewData.View.CameraPosition        = CacheView.PositionWS;
				SceneViewData.View.NearZ                 = CacheView.NearZ;
				SceneViewData.View.FarZ                  = CacheView.FarZ;
				SceneViewData.View.bOrthographic         = CacheView.ProjectionType == EShadowProjectionType::Orthographic;
				SceneViewData.View.Viewport              = BuildShadowViewport(X, Y, Size);
				SceneViewData.View.bShadowStaticOnly     = true;
				SceneViewData.View.bShadowDynamicOnly    = false;

				if (GlobalFilterMode == EShadowFilterMode::Raw || GlobalFilterMode == EShadowFilterMode::PCF)
				{
					BeginPass(Renderer, 0, nullptr, LocalShadowCacheDepthAtlasDSV, SceneViewData.View.Viewport, SceneViewData.Frame, SceneViewData.View);
					Processor.ExecutePass(Renderer, Targets, SceneViewData, EMeshPassType::DepthPrepass);
				}
				else if (GlobalFilterMode == EShadowFilterMode::VSM)
				{
					BeginPass(Renderer, LocalShadowCacheMomentsAtlasRTV, LocalShadowCacheDepthAtlasDSV, SceneViewData.View.Viewport, SceneViewData.Frame, SceneViewData.View);
					Processor.ExecutePass(Renderer, Targets, SceneViewData, EMeshPassType::ShadowVSM);
				}
				else if(GlobalFilterMode == EShadowFilterMode::ESM)
				{
					BeginPass(Renderer, LocalShadowCacheMomentsAtlasRTV, LocalShadowCacheDepthAtlasDSV, SceneViewData.View.Viewport, SceneViewData.Frame, SceneViewData.View);
					Processor.ExecutePass(Renderer, Targets, SceneViewData, EMeshPassType::ShadowESM);
				}

				Light.bCacheDirty = false;
				
			}
		}
	}

	////////////////////////////////////////////////////////////////////
	// 단계 B — 메인 섀도우 패스 (동적 메시 + 캐시 복사)
	////////////////////////////////////////////////////////////////////
	for (uint32 ViewIndex : RenderOrder)
	{
		FShadowViewRenderItem& ShadowView = SceneViewData.LightingInputs.ShadowViews[ViewIndex];

		if (ShadowView.ArraySlice >= ShadowConfig::MaxShadowViews) continue;

		D3D11_VIEWPORT ShadowViewport = {};
		ID3D11DepthStencilView* ShadowDSV = nullptr;
		ID3D11RenderTargetView* MomentsRTV = nullptr;

		const FShadowLightRenderItem& Light = SceneViewData.LightingInputs.ShadowLights[ShadowView.ShadowLightIndex];

		switch (ShadowView.LightType)
		{
		case EShadowLightType::Spot:
		{
			if (!ShadowView.bAtlasAllocated) continue;
			int X = static_cast<int>(ShadowView.AtlasUV.X);
			int Y = static_cast<int>(ShadowView.AtlasUV.Y);
			int Size = ShadowView.AllocatedResolution;

			ShadowDSV = LocalShadowDepthAtlasDSV;
			MomentsRTV = LocalShadowMomentsAtlasRTV;
			ShadowViewport = BuildShadowViewport(X, Y, Size);

			if (Light.Mobility == ELightMobility::Movable)
			{
				SceneViewData.View.bShadowDynamicOnly = false;
				SceneViewData.View.bShadowStaticOnly  = false;
			}
			else
			{
				D3D11_BOX Box;
				Box.left = X; Box.right = X + Size;
				Box.top = Y; Box.bottom = Y + Size;
				Box.front = 0; Box.back = 1;

				if (LocalShadowCacheDepthAtlas)
				{
					DeviceContext->CopySubresourceRegion(LocalShadowDepthAtlas, 0, X, Y, 0, LocalShadowCacheDepthAtlas, 0, &Box);
				}
				if (LocalShadowCacheMomentsAtlas && LocalShadowMomentsAtlas)
				{
					DeviceContext->CopySubresourceRegion(LocalShadowMomentsAtlas, 0, X, Y, 0, LocalShadowCacheMomentsAtlas, 0, &Box);
				}
				
				SceneViewData.View.bShadowDynamicOnly = true;
				SceneViewData.View.bShadowStaticOnly  = false;
			}
			break;
		}
		case EShadowLightType::Point:
		{
			ShadowDSV  = ShadowDepthCubeDSVs[ShadowView.ArraySlice];
			MomentsRTV = ShadowMomentsCubeRTVs[ShadowView.ArraySlice];
			ShadowViewport = BuildShadowViewport(0, 0, ShadowDepthArrayResolution);

			if (Light.Mobility == ELightMobility::Movable)
			{
				DeviceContext->ClearRenderTargetView(MomentsRTV, ClearMoments);
				DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
				SceneViewData.View.bShadowDynamicOnly = false;
				SceneViewData.View.bShadowStaticOnly  = false;
			}
			else
			{
				DeviceContext->CopySubresourceRegion(ShadowDepthCubeArray, ShadowView.ArraySlice, 0, 0, 0, ShadowCacheDepthCube, ShadowView.ArraySlice, nullptr);
				DeviceContext->CopySubresourceRegion(ShadowMomentsCubeArray, ShadowView.ArraySlice, 0, 0, 0, ShadowCacheMomentsCube, ShadowView.ArraySlice, nullptr);

				SceneViewData.View.bShadowDynamicOnly = true;
				SceneViewData.View.bShadowStaticOnly  = false;
			}
			break;
		}
		}

		SceneViewData.View.View                  = ShadowView.View;
		SceneViewData.View.Projection            = ShadowView.Projection;
		SceneViewData.View.ViewProjection        = ShadowView.ViewProjection;
		SceneViewData.View.InverseView           = ShadowView.View.GetInverse();
		SceneViewData.View.InverseProjection     = ShadowView.Projection.GetInverse();
		SceneViewData.View.InverseViewProjection = ShadowView.ViewProjection.GetInverse();
		SceneViewData.View.CameraPosition        = ShadowView.PositionWS;
		SceneViewData.View.NearZ                 = ShadowView.NearZ;
		SceneViewData.View.FarZ                  = ShadowView.FarZ;
		SceneViewData.View.bOrthographic         = ShadowView.ProjectionType == EShadowProjectionType::Orthographic;
		SceneViewData.View.Viewport              = ShadowViewport;

		EMeshPassType PassType = EMeshPassType::DepthPrepass;

		switch (GlobalFilterMode)
		{
			case EShadowFilterMode::VSM:
				if (!MomentsRTV) continue;
				PassType = EMeshPassType::ShadowVSM;
				break;
			case EShadowFilterMode::ESM:
			{
				if (!MomentsRTV) continue;
				PassType = EMeshPassType::ShadowESM;
				UpdateESMConstantBuffer(DeviceContext, Light.ESMExponent);
				break;
			}
			default:
				PassType = EMeshPassType::DepthPrepass;
				break;
		}

		BeginPass(Renderer, MomentsRTV, ShadowDSV, ShadowViewport, SceneViewData.Frame, SceneViewData.View);
		Processor.ExecutePass(Renderer, Targets, SceneViewData, PassType);

	}

	CachedLocalShadowViews.clear();
	for (uint32 ViewIndex = 0; ViewIndex < ShadowViewCount; ++ViewIndex)
	{
		CachedLocalShadowViews.push_back(SceneViewData.LightingInputs.ShadowViews[ViewIndex]);
	}

	SceneViewData.View = OriginalView;

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		OriginalView.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
}
void FShadowRenderFeature::RenderDirectionalShadows(
	FRenderer& Renderer, 
	const FMeshPassProcessor& Processor, 
	FSceneRenderTargets& Targets, 
	FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext && DirShadowAtlasAllocator == nullptr)
	{
		return;
	}

	const uint32 DirShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()),
		ShadowConfig::MaxDirCascade);

	if (DirShadowViewCount == 0)
	{
		return;
	}

	if (!HasShadowVSMCaster(SceneViewData))
	{
		if (!ShouldPreserveAtlasForAuxiliaryPass(SceneViewData))
		{
			ClearShadowAtlasState(Renderer);
		}
		return;
	}

	ID3D11DepthStencilView* DirShadowDSV = DirShadowDepthAtlasDSV;
	if (!DirShadowDSV)
	{
		return;
	}

	DirShadowAtlasAllocator->Reset();
	const FViewContext OriginalView = SceneViewData.View;
	static const float ClearMoments[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	DeviceContext->ClearRenderTargetView(DirShadowMomentsAtlasRTV, ClearMoments);
	DeviceContext->ClearDepthStencilView(DirShadowDepthAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

	for (uint32 ViewIndex = 0; ViewIndex < DirShadowViewCount; ++ViewIndex)
	{
		FShadowViewRenderItem& DirShadowView = SceneViewData.LightingInputs.DirShadowViews[ViewIndex];
		const FShadowLightRenderItem& Light = SceneViewData.LightingInputs.DirShadowLights[DirShadowView.ShadowLightIndex];

		if (DirShadowView.ArraySlice >= ShadowConfig::MaxDirCascade)
		{
			continue;
		}

		FShadowAtlasAllocation OutAllocation;
		if (!DirShadowAtlasAllocator->Allocate(DirShadowView.RequestedResolution, OutAllocation))
		{
			continue;
		}

		DirShadowView.AtlasUV = FVector(static_cast<float>(OutAllocation.X), static_cast<float>(OutAllocation.Y), static_cast<float>(OutAllocation.Size));
		DirShadowView.AllocatedResolution = OutAllocation.Size;
		DirShadowView.bAtlasAllocated = true;

		D3D11_VIEWPORT DirShadowViewport = BuildShadowViewport(OutAllocation.X, OutAllocation.Y, OutAllocation.Size);

		SceneViewData.View.View = DirShadowView.View;
		SceneViewData.View.Projection = DirShadowView.Projection;
		SceneViewData.View.ViewProjection = DirShadowView.ViewProjection;
		SceneViewData.View.InverseView = DirShadowView.View.GetInverse();
		SceneViewData.View.InverseProjection = DirShadowView.Projection.GetInverse();
		SceneViewData.View.InverseViewProjection = DirShadowView.ViewProjection.GetInverse();
		SceneViewData.View.CameraPosition = DirShadowView.PositionWS;
		SceneViewData.View.NearZ = DirShadowView.NearZ;
		SceneViewData.View.FarZ = DirShadowView.FarZ;
		SceneViewData.View.bOrthographic = DirShadowView.ProjectionType == EShadowProjectionType::Orthographic;
		SceneViewData.View.Viewport = DirShadowViewport;

		EMeshPassType PassType = EMeshPassType::DepthPrepass;
		ID3D11RenderTargetView* DirMomentsRTV = nullptr;
		switch (GlobalFilterMode)
		{
		case EShadowFilterMode::VSM:
			if (!DirShadowMomentsAtlasRTV) continue;
			DirMomentsRTV = DirShadowMomentsAtlasRTV;
			PassType = EMeshPassType::ShadowVSM;
			break;
		case EShadowFilterMode::ESM:
		{
			if (!DirShadowMomentsAtlasRTV) continue;
			DirMomentsRTV = DirShadowMomentsAtlasRTV;
			PassType = EMeshPassType::ShadowESM;
			UpdateESMConstantBuffer(DeviceContext, Light.ESMExponent);
			break;
		}
		default:
			PassType = EMeshPassType::DepthPrepass;
			break;
		}

		BeginPass(Renderer, DirMomentsRTV, DirShadowDSV, DirShadowViewport, SceneViewData.Frame, SceneViewData.View);
		Processor.ExecutePass(Renderer, Targets, SceneViewData, PassType);
	}

	CachedDirShadowViews.clear();
	for (uint32 ViewIndex = 0; ViewIndex < DirShadowViewCount; ++ViewIndex)
	{
		CachedDirShadowViews.push_back(SceneViewData.LightingInputs.DirShadowViews[ViewIndex]);
	}

	SceneViewData.View = OriginalView;

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		OriginalView.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
}

uint32 FShadowRenderFeature::ResolveShadowViewResolution(uint32 RequestedResolution) const
{
	const uint32 Resolution = RequestedResolution > 0
		                          ? RequestedResolution
		                          : DefaultShadowMapResolution;

	return FMath::Clamp(
		Resolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);
}

uint32 FShadowRenderFeature::ComputeRequiredShadowDepthArrayResolution(
	const FSceneViewData& SceneViewData) const
{
	uint32 RequiredResolution = DefaultShadowMapResolution;

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	for (uint32 Index = 0; Index < ShadowViewCount; ++Index)
	{
		const FShadowViewRenderItem& View = SceneViewData.LightingInputs.ShadowViews[Index];
		RequiredResolution                = (std::max)(RequiredResolution, ResolveShadowViewResolution(View.RequestedResolution));
	}

	return FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);
}

D3D11_VIEWPORT FShadowRenderFeature::BuildShadowViewport(int X, int Y, int Size) const
{
	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX       = static_cast<float>(X);
	Viewport.TopLeftY       = static_cast<float>(Y);
	Viewport.Width          = static_cast<float>(Size);
	Viewport.Height         = static_cast<float>(Size);
	Viewport.MinDepth       = 0.0f;
	Viewport.MaxDepth       = 1.0f;

	return Viewport;
}


bool FShadowRenderFeature::EnsureDebugPreviewResources(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 Size = ShadowDepthArrayResolution > 0
		                    ? ShadowDepthArrayResolution
		                    : DefaultShadowMapResolution;

	bool bRecreate =
			!ShadowDebugPreviewTexture ||
			!ShadowDebugPreviewRTV ||
			!ShadowDebugPreviewSRV;

	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		ShadowDebugPreviewTexture->GetDesc(&ExistingDesc);

		if (ExistingDesc.Width != Size ||
			ExistingDesc.Height != Size ||
			ExistingDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			bRecreate = true;
		}
	}

	if (bRecreate)
	{
		SafeRelease(ShadowDebugPreviewSRV);
		SafeRelease(ShadowDebugPreviewRTV);
		SafeRelease(ShadowDebugPreviewTexture);

		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width                = Size;
		Desc.Height               = Size;
		Desc.MipLevels            = 1;
		Desc.ArraySize            = 1;
		Desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count     = 1;
		Desc.SampleDesc.Quality   = 0;
		Desc.Usage                = D3D11_USAGE_DEFAULT;
		Desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &ShadowDebugPreviewTexture)) ||
			!ShadowDebugPreviewTexture)
		{
			return false;
		}

		if (FAILED(Device->CreateRenderTargetView(ShadowDebugPreviewTexture, nullptr, &ShadowDebugPreviewRTV)) ||
			!ShadowDebugPreviewRTV)
		{
			SafeRelease(ShadowDebugPreviewTexture);
			return false;
		}

		if (FAILED(Device->CreateShaderResourceView(ShadowDebugPreviewTexture, nullptr, &ShadowDebugPreviewSRV)) ||
			!ShadowDebugPreviewSRV)
		{
			SafeRelease(ShadowDebugPreviewRTV);
			SafeRelease(ShadowDebugPreviewTexture);
			return false;
		}
	}

	if (!ShadowDebugSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.MinLOD             = 0.0f;
		SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &ShadowDebugSampler)) ||
			!ShadowDebugSampler)
		{
			return false;
		}
	}

	if (!ShadowDebugCB)
	{
		struct FShadowDebugCBData
		{
			uint32 DebugMode;
			uint32 SliceIndex;
			float  NearZ;
			float  FarZ;

			uint32 bOrthographic;
			float  Exposure;
			float  Padding0;
			float  Padding1;
		};

		D3D11_BUFFER_DESC CBDesc = {};
		CBDesc.ByteWidth         = sizeof(FShadowDebugCBData);
		CBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		CBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		CBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, &ShadowDebugCB)) ||
			!ShadowDebugCB)
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	if (!ShadowDebugVS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Vertex;
		Recipe.SourcePath    = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "vs_5_0";
		Recipe.LayoutType    = EVertexLayoutType::FullscreenNone;

		ShadowDebugVS = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
	}

	if (!ShadowDebugPS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Pixel;
		Recipe.SourcePath    = ShaderDir + L"Shadow/ShadowDebugPreviewPixelShader.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "ps_5_0";

		ShadowDebugPS = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	if (!ShadowAtlasPreviewPS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Pixel;
		Recipe.SourcePath    = ShaderDir + L"Shadow/ShadowAtlasPreviewPixelShader.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "ps_5_0";

		ShadowAtlasPreviewPS = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	return ShadowDebugVS != nullptr && ShadowDebugPS != nullptr && ShadowAtlasPreviewPS != nullptr;
}

bool FShadowRenderFeature::EnsureAtlasPreviewTexture(
	FRenderer& Renderer,
	uint32 Size,
	ID3D11Texture2D*& Texture,
	ID3D11RenderTargetView*& RTV,
	ID3D11ShaderResourceView*& SRV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device || Size == 0)
	{
		return false;
	}

	bool bRecreate = !Texture || !RTV || !SRV;
	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		Texture->GetDesc(&ExistingDesc);
		bRecreate =
			ExistingDesc.Width != Size ||
			ExistingDesc.Height != Size ||
			ExistingDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(SRV);
	SafeRelease(RTV);
	SafeRelease(Texture);

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = Size;
	Desc.Height = Size;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Texture)) || !Texture)
	{
		return false;
	}

	if (FAILED(Device->CreateRenderTargetView(Texture, nullptr, &RTV)) || !RTV)
	{
		SafeRelease(Texture);
		return false;
	}

	if (FAILED(Device->CreateShaderResourceView(Texture, nullptr, &SRV)) || !SRV)
	{
		SafeRelease(RTV);
		SafeRelease(Texture);
		return false;
	}

	return true;
}

bool FShadowRenderFeature::RenderAtlasPreview(
	FRenderer& Renderer,
	const FSceneViewData& SceneViewData,
	ID3D11ShaderResourceView* SourceSRV,
	uint32 Size,
	ID3D11RenderTargetView* TargetRTV)
{
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !SourceSRV || !TargetRTV || !ShadowDebugCB || !ShadowDebugVS || !ShadowAtlasPreviewPS)
	{
		return false;
	}

	struct FShadowAtlasPreviewCBData
	{
		float Exposure;
		float Padding0;
		float Padding1;
		float Padding2;
	};

	FShadowAtlasPreviewCBData CBData = {};
	CBData.Exposure = 24.0f;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(ShadowDebugCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, &CBData, sizeof(CBData));
	Context->Unmap(ShadowDebugCB, 0);

	const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	Context->ClearRenderTargetView(TargetRTV, ClearColor);

	D3D11_VIEWPORT PreviewViewport = {};
	PreviewViewport.TopLeftX = 0.0f;
	PreviewViewport.TopLeftY = 0.0f;
	PreviewViewport.Width = static_cast<float>(Size);
	PreviewViewport.Height = static_cast<float>(Size);
	PreviewViewport.MinDepth = 0.0f;
	PreviewViewport.MaxDepth = 1.0f;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, ShadowDebugCB },
	};
	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, SourceSRV },
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, ShadowDebugSampler },
	};
	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	return ExecuteFullscreenPass(
		Renderer,
		SceneViewData.Frame,
		SceneViewData.View,
		TargetRTV,
		nullptr,
		PreviewViewport,
		{ ShadowDebugVS, ShadowAtlasPreviewPS },
		{},
		Bindings,
		[](ID3D11DeviceContext& DrawContext)
		{
			DrawContext.Draw(3, 0);
		});
}

void FShadowRenderFeature::RenderShadowAtlasPreviews(FRenderer& Renderer, const FSceneViewData& SceneViewData)
{
	if (!EnsureDebugPreviewResources(Renderer))
	{
		return;
	}

	if (LocalShadowDepthAtlasSRV &&
		EnsureAtlasPreviewTexture(
			Renderer,
			ShadowConfig::MaxShadowMapResolution,
			LocalShadowAtlasPreviewTexture,
			LocalShadowAtlasPreviewRTV,
			LocalShadowAtlasPreviewSRV))
	{
		RenderAtlasPreview(
			Renderer,
			SceneViewData,
			LocalShadowDepthAtlasSRV,
			ShadowConfig::MaxShadowMapResolution,
			LocalShadowAtlasPreviewRTV);
	}

	SafeRelease(DirShadowAtlasPreviewSRV);
	SafeRelease(DirShadowAtlasPreviewRTV);
	SafeRelease(DirShadowAtlasPreviewTexture);
}

bool FShadowRenderFeature::RenderDebugPreview(
	FRenderer&            Renderer,
	FSceneRenderTargets&  Targets,
	const FSceneViewData& SceneViewData)
{
	if (DebugViewMode == EShadowDebugViewMode::None)
	{
		return true;
	}

	ID3D11ShaderResourceView* TargetDepthSRV = bDebugDirectional ? DirShadowDepthAtlasSRV : LocalShadowDepthAtlasSRV;
	ID3D11ShaderResourceView* TargetMomentsSRV = bDebugDirectional ? DirShadowMomentsAtlasSRV : LocalShadowMomentsAtlasSRV;
	const auto& TargetViews = bDebugDirectional ? SceneViewData.LightingInputs.DirShadowViews : SceneViewData.LightingInputs.ShadowViews;
	if (!TargetDepthSRV) return false;

	if ((DebugViewMode == EShadowDebugViewMode::VSMMean 
		|| DebugViewMode == EShadowDebugViewMode::VSMVariance)
		&& !TargetMomentsSRV)
	{
		return false;
	}

	if (!EnsureDebugPreviewResources(Renderer))
	{
		return false;
	}

	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !ShadowDebugPreviewRTV || !ShadowDebugCB)
	{
		return false;
	}

	const uint32 ShadowViewCount = static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size());
	const uint32 DirShadowViewCount = static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size());
	if (ShadowViewCount == 0 && DirShadowViewCount == 0)
	{
		return false;
	}

	DebugAvailableSlices.clear();

	for (const FShadowViewRenderItem& View : TargetViews)
	{
		uint32 MaxLimit = bDebugDirectional ? ShadowConfig::MaxDirCascade : ShadowConfig::MaxShadowViews;
		if (View.ArraySlice < MaxLimit)
		{
			if (std::find(DebugAvailableSlices.begin(), DebugAvailableSlices.end(), View.ArraySlice) == DebugAvailableSlices.end())
			{
				DebugAvailableSlices.push_back(View.ArraySlice);
			}
		}
	}

	std::sort(DebugAvailableSlices.begin(), DebugAvailableSlices.end());

	if (DebugAvailableSlices.empty())
	{
		return false;
	}

	const uint32 RequestedSlice = (std::min)(DebugViewSlice, (bDebugDirectional ? ShadowConfig::MaxDirCascade : ShadowConfig::MaxShadowViews) - 1u);

	const FShadowViewRenderItem* SelectedView = nullptr;
	for (const FShadowViewRenderItem& View : TargetViews)
	{
		if (View.ArraySlice == RequestedSlice)
		{
			SelectedView = &View;
			break;
		}
	}

	if (!SelectedView)
	{
		DebugViewSlice = DebugAvailableSlices[0];

		for (const FShadowViewRenderItem& View : SceneViewData.LightingInputs.ShadowViews)
		{
			if (View.ArraySlice == DebugViewSlice)
			{
				SelectedView = &View;
				break;
			}
		}
	}

	if (!SelectedView)
	{
		return false;
	}

	struct FShadowDebugCBData
	{
		uint32 DebugMode;
		uint32 SliceIndex;
		float  NearZ;
		float  FarZ;

		uint32 bOrthographic;
		float  Exposure;
		float  Padding0;
		float  Padding1;
	};

	FShadowDebugCBData CBData = {};
	CBData.DebugMode          = static_cast<uint32>(DebugViewMode);
	CBData.SliceIndex         = SelectedView->ArraySlice;
	CBData.NearZ              = SelectedView->NearZ;
	CBData.FarZ               = SelectedView->FarZ;
	CBData.bOrthographic      = SelectedView->ProjectionType == EShadowProjectionType::Orthographic ? 1u : 0u;
	CBData.Exposure           = DebugVarianceExposure;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(ShadowDebugCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, &CBData, sizeof(CBData));
	Context->Unmap(ShadowDebugCB, 0);

	const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	Context->ClearRenderTargetView(ShadowDebugPreviewRTV, ClearColor);

	const float Size = static_cast<float>(
		ShadowDepthArrayResolution > 0
			? ShadowDepthArrayResolution
			: DefaultShadowMapResolution);

	D3D11_VIEWPORT PreviewViewport = {};
	PreviewViewport.TopLeftX       = 0.0f;
	PreviewViewport.TopLeftY       = 0.0f;
	PreviewViewport.Width          = Size;
	PreviewViewport.Height         = Size;
	PreviewViewport.MinDepth       = 0.0f;
	PreviewViewport.MaxDepth       = 1.0f;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, ShadowDebugCB },
	};

	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, TargetDepthSRV },
		{ 1, TargetMomentsSRV },
	};

	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, ShadowDebugSampler },
	};

	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	const bool bRendered = ExecuteFullscreenPass(
		Renderer,
		SceneViewData.Frame,
		SceneViewData.View,
		ShadowDebugPreviewRTV,
		nullptr,
		PreviewViewport,
		{ ShadowDebugVS, ShadowDebugPS },
		{},
		Bindings,
		[](ID3D11DeviceContext& DrawContext)
		{
			DrawContext.Draw(3, 0);
		});

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);

	return bRendered;
}
