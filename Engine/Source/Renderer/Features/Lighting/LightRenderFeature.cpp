#include "Renderer/Features/Lighting/LightRenderFeature.h"

#include <algorithm>
#include <cstring>

#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"
#include "Renderer/Scene/SceneViewData.h"

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

	UINT Align16(UINT Value)
	{
		return (Value + 15u) & ~15u;
	}

	double TicksToMilliseconds(uint64 StartTick, uint64 EndTick, uint64 Frequency)
	{
		if (EndTick <= StartTick || Frequency == 0u)
		{
			return 0.0;
		}

		return static_cast<double>(EndTick - StartTick) * 1000.0 / static_cast<double>(Frequency);
	}

	bool UploadDynamicBuffer(
		ID3D11DeviceContext* DeviceContext,
		ID3D11Buffer*        Buffer,
		const void*          Data,
		size_t               SizeInBytes)
	{
		if (!DeviceContext || !Buffer)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(DeviceContext->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return false;
		}

		memcpy(Mapped.pData, Data, SizeInBytes);
		DeviceContext->Unmap(Buffer, 0);
		return true;
	}

	bool NeedObejctLightIndexUpload(const FSceneViewData& SceneViewData)
	{
		switch (SceneViewData.RenderMode)
		{
		case ERenderMode::Lit_Gouraud:
			return true;
		case ERenderMode::Lit_Lambert:
		case ERenderMode::Lit_Phong:
		case ERenderMode::Unlit:
		default:
			return false;
		}
	}
}

FLightRenderFeature::~FLightRenderFeature()
{
	Release();
}

bool FLightRenderFeature::PrepareClusteredLightResources(
	FRenderer&                 Renderer,
	const FSceneViewData&      SceneViewData,
	const FSceneRenderTargets& Targets)
{
	if (!Initialize(Renderer))
	{
		return false;
	}

	if (ID3D11DeviceContext* TimingContext = Renderer.GetDeviceContext())
	{
		ResolveTimingQueries(TimingContext);
	}

	if (bHasPendingReadback && ClusterHeaderStagingBuffer)
	{
		ID3D11DeviceContext*     DC     = Renderer.GetDeviceContext();
		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (DC && SUCCEEDED(DC->Map(ClusterHeaderStagingBuffer, 0, D3D11_MAP_READ, 0, &Mapped)))
		{
			auto   Headers          = static_cast<const FLightClusterHeaderGPU*>(Mapped.pData);
			uint32 TotalAssignments = 0;
			uint32 OverflowDropped  = 0;
			for (uint32 i = 0; i < PendingReadbackClusterCount; ++i)
			{
				TotalAssignments      += Headers[i].Count;
				const uint32 RawCount = Headers[i].Pad0;
				if (RawCount > Headers[i].Count)
				{
					OverflowDropped += RawCount - Headers[i].Count;
				}
			}
			Stats.TotalLightClusterAssignments = TotalAssignments;
			Stats.OverflowCulledSlots          = OverflowDropped;
			DC->Unmap(ClusterHeaderStagingBuffer, 0);
		}
		bHasPendingReadback = false;
	}

	UpdateClusterGlobalConstantBuffer(Renderer, SceneViewData);
	UploadLocalLightBuffers(Renderer, SceneViewData);

	if (SceneViewData.RenderMode == ERenderMode::Unlit || SceneViewData.RenderMode == ERenderMode::WorldNormal)
	{
		return true;
	}

	if (!Targets.SceneDepthSRV)
	{
		return false;
	}

	const uint32     ViewportWidth      = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Width, 0.0f));
	const uint32     ViewportHeight     = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Height, 0.0f));
	const uint32     ClusterCountX      = (ViewportWidth + LightCullingConfig::TileSizeX - 1u) / LightCullingConfig::TileSizeX;
	const uint32     ClusterCountY      = (ViewportHeight + LightCullingConfig::TileSizeY - 1u) / LightCullingConfig::TileSizeY;
	constexpr uint32 ClusterCountZ      = LightCullingConfig::ClusterCountZ;
	const uint32     ClusterCount       = ClusterCountX * ClusterCountY * ClusterCountZ;
	const uint32     TileCount          = ClusterCountX * ClusterCountY;
	const uint32     ClusterHeaderCount = ClusterCount;

	Stats.ClusterCountX            = ClusterCountX;
	Stats.ClusterCountY            = ClusterCountY;
	Stats.ClusterCountZ            = ClusterCountZ;
	Stats.TotalClusters            = ClusterCount;
	Stats.MaxLightsPerCluster      = LightListConfig::MaxLightsPerCluster;
	Stats.MaxLocalLights           = LightListConfig::MaxLocalLights;
	const uint32 ClusterIndexCount = ClusterCount * LightListConfig::MaxLightsPerCluster;

	Stats.TileDepthBoundsBufferBytes = sizeof(FTileDepthBoundsGPU) * (std::max)(1u, TileCount);
	Stats.ClusterHeaderBufferBytes   = sizeof(FLightClusterHeaderGPU) * (std::max)(1u, ClusterHeaderCount);
	Stats.ClusterIndexBufferBytes    = sizeof(uint32) * (std::max)(1u, ClusterIndexCount);
	Stats.TotalBufferBytes           = Stats.LocalLightBufferBytes + Stats.CullProxyBufferBytes
			+ Stats.TileDepthBoundsBufferBytes + Stats.ClusterHeaderBufferBytes
			+ Stats.ClusterIndexBufferBytes;

	if (!EnsureDefaultStructuredBufferSRVUAV(
		Renderer,
		sizeof(FTileDepthBoundsGPU),
		(std::max)(1u, TileCount),
		TileDepthBoundsBuffer,
		TileDepthBoundsSRV,
		TileDepthBoundsUAV))
	{
		return false;
	}

	if (!EnsureDefaultStructuredBufferSRVUAV(
		Renderer,
		sizeof(FLightClusterHeaderGPU),
		(std::max)(1u, ClusterHeaderCount),
		ClusterLightHeaderBuffer,
		ClusterLightHeaderSRV,
		ClusterLightHeaderUAV))
	{
		return false;
	}

	if (!EnsureDefaultStructuredBufferSRVUAV(
		Renderer,
		sizeof(uint32),
		(std::max)(1u, ClusterIndexCount),
		ClusterLightIndexBuffer,
		ClusterLightIndexSRV,
		ClusterLightIndexUAV))
	{
		return false;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	constexpr UINT ClearValues[4] = { 0, 0, 0, 0 };
	DeviceContext->ClearUnorderedAccessViewUint(ClusterLightHeaderUAV, ClearValues);
	DeviceContext->ClearUnorderedAccessViewUint(ClusterLightIndexUAV, ClearValues);

	FLightTimingQuerySet* ActiveTimingQuerySet = nullptr;
	if (EnsureTimingQueries(Renderer))
	{
		FLightTimingQuerySet& TimingQuerySet = TimingQuerySets[TimingQueryWriteIndex];
		if (!TimingQuerySet.bPending)
		{
			DeviceContext->Begin(TimingQuerySet.Disjoint);
			DeviceContext->End(TimingQuerySet.TileDepthBoundsStart);
			ActiveTimingQuerySet = &TimingQuerySet;
		}
	}

	TileDepthBoundsCS->Bind(DeviceContext);
	DeviceContext->CSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);

	ID3D11ShaderResourceView*  TileDepthSRV[1]     = { Targets.SceneDepthSRV };
	ID3D11UnorderedAccessView* TileDepthUAV[1]     = { TileDepthBoundsUAV };
	UINT                       TileInitialCount[1] = { 0u };

	DeviceContext->CSSetShaderResources(0, 1, TileDepthSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, TileDepthUAV, TileInitialCount);
	DeviceContext->Dispatch(ClusterCountX, ClusterCountY, 1u);

	if (ActiveTimingQuerySet)
	{
		DeviceContext->End(ActiveTimingQuerySet->TileDepthBoundsEnd);
		DeviceContext->End(ActiveTimingQuerySet->LightCullingStart);
	}

	ID3D11ShaderResourceView*  NullTileSRV[1] = { nullptr };
	ID3D11UnorderedAccessView* NullTileUAV[1] = { nullptr };
	DeviceContext->CSSetShaderResources(0, 1, NullTileSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, NullTileUAV, TileInitialCount);

	LightCullingCS->Bind(DeviceContext);
	DeviceContext->CSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);

	ID3D11ShaderResourceView* CSRVs[2] = { LightCullProxySRV, TileDepthBoundsSRV };
	DeviceContext->CSSetShaderResources(0, 2, CSRVs);

	ID3D11UnorderedAccessView* CSUAVs[2]           = { ClusterLightHeaderUAV, ClusterLightIndexUAV };
	UINT                       UAVInitialCounts[2] = { 0u, 0u };
	DeviceContext->CSSetUnorderedAccessViews(0, 2, CSUAVs, UAVInitialCounts);

	DeviceContext->Dispatch(ClusterCountX, ClusterCountY, ClusterCountZ);

	if (ActiveTimingQuerySet)
	{
		DeviceContext->End(ActiveTimingQuerySet->LightCullingEnd);
		DeviceContext->End(ActiveTimingQuerySet->Disjoint);
		ActiveTimingQuerySet->bPending = true;
		TimingQueryWriteIndex          = (TimingQueryWriteIndex + 1u) % TimingQueryBufferCount;
	}

	ID3D11ShaderResourceView*  NullSRV[2] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* NullUAV[2] = { nullptr, nullptr };
	ID3D11Buffer*              NullCB[1]  = { nullptr };

	DeviceContext->CSSetShaderResources(0, 2, NullSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 2, NullUAV, UAVInitialCounts);
	DeviceContext->CSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, NullCB);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);

	if (ClusterLightHeaderBuffer &&
		EnsureStagingBuffer(Renderer, sizeof(FLightClusterHeaderGPU), (std::max)(1u, ClusterCount), ClusterHeaderStagingBuffer))
	{
		DeviceContext->CopyResource(ClusterHeaderStagingBuffer, ClusterLightHeaderBuffer);
		PendingReadbackClusterCount = ClusterCount;
		bHasPendingReadback         = true;
	}

	return true;
}

bool FLightRenderFeature::Render(
	FRenderer&                 Renderer,
	const FSceneViewData&      SceneViewData,
	const FSceneRenderTargets& Targets)
{
	if (!Initialize(Renderer))
	{
		return false;
	}

	UpdateGlobalLightConstantBuffer(Renderer, SceneViewData);

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	DeviceContext->VSSetConstantBuffers(LightClusterSlots::GlobalLightCB, 1, &GlobalLightConstantBuffer);
	DeviceContext->PSSetConstantBuffers(LightClusterSlots::GlobalLightCB, 1, &GlobalLightConstantBuffer);
	DeviceContext->VSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);
	DeviceContext->PSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);

	if (LocalLightSRV)
	{
		DeviceContext->VSSetShaderResources(LightClusterSlots::LocalLightSRV, 1, &LocalLightSRV);
		DeviceContext->PSSetShaderResources(LightClusterSlots::LocalLightSRV, 1, &LocalLightSRV);
	}

	if (ObjectLightIndexSRV)
	{
		DeviceContext->VSSetShaderResources(LightClusterSlots::ObjectLightIndexSRV, 1, &ObjectLightIndexSRV);
	}

	if (ClusterLightHeaderSRV)
	{
		DeviceContext->PSSetShaderResources(LightClusterSlots::ClusterLightHeaderSRV, 1, &ClusterLightHeaderSRV);
	}

	if (ClusterLightIndexSRV)
	{
		DeviceContext->PSSetShaderResources(LightClusterSlots::ClusterLightIndexSRV, 1, &ClusterLightIndexSRV);
	}

	return true;
}

void FLightRenderFeature::Release()
{
	SafeRelease(GlobalLightConstantBuffer);
	SafeRelease(ClusterGlobalConstantBuffer);

	SafeRelease(LocalLightSRV);
	SafeRelease(LocalLightBuffer);

	SafeRelease(LightCullProxySRV);
	SafeRelease(LightCullProxyBuffer);

	SafeRelease(ObjectLightIndexSRV);
	SafeRelease(ObjectLightIndexBuffer);

	SafeRelease(ClusterLightHeaderUAV);
	SafeRelease(ClusterLightHeaderSRV);
	SafeRelease(ClusterLightHeaderBuffer);

	SafeRelease(ClusterLightIndexUAV);
	SafeRelease(ClusterLightIndexSRV);
	SafeRelease(ClusterLightIndexBuffer);

	SafeRelease(TileDepthBoundsUAV);
	SafeRelease(TileDepthBoundsSRV);
	SafeRelease(TileDepthBoundsBuffer);

	for (FLightTimingQuerySet& TimingQuerySet : TimingQuerySets)
	{
		SafeRelease(TimingQuerySet.Disjoint);
		SafeRelease(TimingQuerySet.TileDepthBoundsStart);
		SafeRelease(TimingQuerySet.TileDepthBoundsEnd);
		SafeRelease(TimingQuerySet.LightCullingStart);
		SafeRelease(TimingQuerySet.LightCullingEnd);
		TimingQuerySet.bPending = false;
	}
	TimingQueryWriteIndex = 0;

	SafeRelease(ClusterHeaderStagingBuffer);

	TileDepthBoundsCS.reset();
	LightCullingCS.reset();

	for (uint32 VariantIndex = 0; VariantIndex < ShaderVariantCount; ++VariantIndex)
	{
		GouraudVariants[VariantIndex]     = {};
		LambertVariants[VariantIndex]     = {};
		PhongVariants[VariantIndex]       = {};
		ToonVariants[VariantIndex]        = {};
		WorldNormalVariants[VariantIndex] = {};
	}
	SafeRelease(DepthSampler);
}

std::shared_ptr<FVertexShaderHandle> FLightRenderFeature::GetCurrentVSHandle(bool bHasNormalMap, ERenderMode RenderMode) const
{
	const uint32 Variant = ToShaderVariantIndex(bHasNormalMap);
	if (RenderMode == ERenderMode::WorldNormal)
	{
		return WorldNormalVariants[Variant].VertexHandle;
	}
	switch (CurrentLightingModel)
	{
	case ELightingModel::Lambert: return LambertVariants[Variant].VertexHandle;
	case ELightingModel::Phong: return PhongVariants[Variant].VertexHandle;
	case ELightingModel::Toon: return ToonVariants[Variant].VertexHandle;
	default: return GouraudVariants[Variant].VertexHandle;
	}
}

std::shared_ptr<FPixelShaderHandle> FLightRenderFeature::GetCurrentPSHandle(bool bHasNormalMap, ERenderMode RenderMode) const
{
	const uint32 Variant = ToShaderVariantIndex(bHasNormalMap);
	if (RenderMode == ERenderMode::WorldNormal)
	{
		return WorldNormalVariants[Variant].PixelHandle;
	}
	switch (CurrentLightingModel)
	{
	case ELightingModel::Lambert: return LambertVariants[Variant].PixelHandle;
	case ELightingModel::Phong: return PhongVariants[Variant].PixelHandle;
	case ELightingModel::Toon: return ToonVariants[Variant].PixelHandle;
	default: return GouraudVariants[Variant].PixelHandle;
	}
}

bool FLightRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!CompileShaderVariants(Renderer))
	{
		return false;
	}

	if (!GlobalLightConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage             = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth         = Align16(sizeof(FGlobalLightConstantBuffer));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &GlobalLightConstantBuffer)))
		{
			return false;
		}
	}

	if (!ClusterGlobalConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage             = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth         = Align16(sizeof(FLightClusterGlobalCB));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &ClusterGlobalConstantBuffer)))
		{
			return false;
		}
	}

	if (!LightCullingCS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Compute;
		Recipe.SourcePath    = FPaths::ShaderDir().wstring() + L"SceneLighting/LightCullingCS.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "cs_5_0";
		LightCullingCS       = FShaderRegistry::Get().GetOrCreateComputeShaderHandle(Device, Recipe);
	}

	if (!TileDepthBoundsCS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Compute;
		Recipe.SourcePath    = FPaths::ShaderDir().wstring() + L"SceneLighting/TileDepthBoundsCS.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "cs_5_0";
		TileDepthBoundsCS    = FShaderRegistry::Get().GetOrCreateComputeShaderHandle(Device, Recipe);
	}

	if (!DepthSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD             = 0.0f;
		SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &DepthSampler)))
		{
			return false;
		}
	}

	return LightCullingCS != nullptr && TileDepthBoundsCS != nullptr;
}

bool FLightRenderFeature::CompileShaderVariants(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const std::wstring VSPath = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitVertexShader.hlsl";
	const std::wstring PSPath = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitPixelShader.hlsl";

	auto BuildVertexRecipe = [&](const std::wstring& Path, const std::vector<FShaderMacroDesc>& Macros) -> FShaderRecipe
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Vertex;
		Recipe.SourcePath    = Path;
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "vs_5_0";
		Recipe.LayoutType    = EVertexLayoutType::MeshVertex;
		Recipe.Macros        = Macros;
		return Recipe;
	};

	auto BuildPixelRecipe = [&](const std::wstring& Path, const std::vector<FShaderMacroDesc>& Macros) -> FShaderRecipe
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Pixel;
		Recipe.SourcePath    = Path;
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "ps_5_0";
		Recipe.Macros        = Macros;
		return Recipe;
	};

	for (uint32 VariantIndex = 0; VariantIndex < ShaderVariantCount; ++VariantIndex)
	{
		const bool bHasNormalMap = (VariantIndex != 0);

		GouraudVariants[VariantIndex].VertexHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(
			Device,
			BuildVertexRecipe(VSPath, { { "LIGHTING_MODEL_GOURAUD", "1" }, { "VERTEX_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));
		GouraudVariants[VariantIndex].PixelHandle = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(
			Device,
			BuildPixelRecipe(PSPath, { { "LIGHTING_MODEL_GOURAUD", "1" }, { "HAS_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));

		LambertVariants[VariantIndex].VertexHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(
			Device,
			BuildVertexRecipe(VSPath, { { "LIGHTING_MODEL_LAMBERT", "1" } }));
		LambertVariants[VariantIndex].PixelHandle = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(
			Device,
			BuildPixelRecipe(PSPath, { { "LIGHTING_MODEL_LAMBERT", "1" }, { "HAS_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));

		PhongVariants[VariantIndex].VertexHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(
			Device,
			BuildVertexRecipe(VSPath, { { "LIGHTING_MODEL_PHONG", "1" } }));
		PhongVariants[VariantIndex].PixelHandle = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(
			Device,
			BuildPixelRecipe(PSPath, { { "LIGHTING_MODEL_PHONG", "1" }, { "HAS_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));

		ToonVariants[VariantIndex].VertexHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(
			Device,
			BuildVertexRecipe(VSPath, { { "LIGHTING_MODEL_TOON", "1" } }));

		ToonVariants[VariantIndex].PixelHandle = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(
			Device,
			BuildPixelRecipe(PSPath, { { "LIGHTING_MODEL_TOON", "1" }, { "HAS_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));

		WorldNormalVariants[VariantIndex].VertexHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(
			Device,
			BuildVertexRecipe(VSPath, { { "VIEWMODE_WORLD_NORMAL", "1" } }));
		WorldNormalVariants[VariantIndex].PixelHandle = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(
			Device,
			BuildPixelRecipe(PSPath, { { "VIEWMODE_WORLD_NORMAL", "1" }, { "HAS_NORMAL_MAP", bHasNormalMap ? "1" : "0" } }));

		if (!GouraudVariants[VariantIndex].VertexHandle
			|| !GouraudVariants[VariantIndex].PixelHandle
			|| !LambertVariants[VariantIndex].VertexHandle
			|| !LambertVariants[VariantIndex].PixelHandle
			|| !PhongVariants[VariantIndex].VertexHandle
			|| !PhongVariants[VariantIndex].PixelHandle
			|| !ToonVariants[VariantIndex].VertexHandle
			|| !ToonVariants[VariantIndex].PixelHandle
			|| !WorldNormalVariants[VariantIndex].VertexHandle
			|| !WorldNormalVariants[VariantIndex].PixelHandle)
		{
			return false;
		}
	}

	return true;
}

void FLightRenderFeature::UpdateGlobalLightConstantBuffer(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	FGlobalLightConstantBuffer CB = {};

	CB.Ambient.ColorIntensity = FVector4(
		SceneViewData.LightingInputs.Ambient.Color.X,
		SceneViewData.LightingInputs.Ambient.Color.Y,
		SceneViewData.LightingInputs.Ambient.Color.Z,
		SceneViewData.LightingInputs.Ambient.Intensity);

	CB.AmbientEnabled = SceneViewData.LightingInputs.Ambient.Intensity > 0.0f ? 1u : 0u;

	if (!SceneViewData.LightingInputs.DirectionalLights.empty())
	{
		const FDirectionalLightRenderItem& Dir     = SceneViewData.LightingInputs.DirectionalLights[0];
		const FVector                      SafeDir = Dir.DirectionWS.GetSafeNormal();

		CB.Directional.ColorIntensity = FVector4(Dir.Color.X, Dir.Color.Y, Dir.Color.Z, Dir.Intensity);
		CB.Directional.DirectionEtc   = FVector4(SafeDir.X, SafeDir.Y, SafeDir.Z, 0.0f);
		CB.DirectionalLightCount      = 1;
	}
	else
	{
		CB.DirectionalLightCount = 0;
	}

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		GlobalLightConstantBuffer,
		&CB,
		sizeof(CB));
}

void FLightRenderFeature::UpdateClusterGlobalConstantBuffer(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	FLightClusterGlobalCB CB = {};
	CB.View                  = SceneViewData.View.View.GetTransposed();
	CB.Projection            = SceneViewData.View.Projection.GetTransposed();
	CB.InverseProjection     = SceneViewData.View.InverseProjection.GetTransposed();
	CB.InverseView           = SceneViewData.View.View.GetInverse().GetTransposed();

	CB.ClusterCameraPosition = FVector4(
		SceneViewData.View.CameraPosition.X,
		SceneViewData.View.CameraPosition.Y,
		SceneViewData.View.CameraPosition.Z,
		1.0f);

	const float Width  = SceneViewData.View.Viewport.Width;
	const float Height = SceneViewData.View.Viewport.Height;

	CB.ScreenParams = FVector4(
		Width,
		Height,
		Width > 0.0f ? 1.0f / Width : 0.0f,
		Height > 0.0f ? 1.0f / Height : 0.0f);

	const uint32 ViewportWidthPx  = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Width, 0.0f));
	const uint32 ViewportHeightPx = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Height, 0.0f));

	CB.ClusterCountX   = (ViewportWidthPx + LightCullingConfig::TileSizeX - 1u) / LightCullingConfig::TileSizeX;
	CB.ClusterCountY   = (ViewportHeightPx + LightCullingConfig::TileSizeY - 1u) / LightCullingConfig::TileSizeY;
	CB.ClusterCountZ   = LightCullingConfig::ClusterCountZ;
	CB.LocalLightCount = (std::min)(static_cast<uint32>(SceneViewData.LightingInputs.LocalLights.size()), LightListConfig::MaxLocalLights);

	CB.DirectionalLightCount = static_cast<uint32>(SceneViewData.LightingInputs.DirectionalLights.size());
	CB.MaxLightsPerCluster   = LightListConfig::MaxLightsPerCluster;
	CB.LightingEnabled       = SceneViewData.RenderMode != ERenderMode::Unlit ? 1u : 0u;
	CB.VisualizationMode     = SceneViewData.RenderMode == ERenderMode::LightCullingHeatmap ? 1u : 0u;

	CB.NearZ = (std::max)(SceneViewData.View.NearZ, 1e-4f);
	CB.FarZ  = (std::max)(SceneViewData.View.FarZ, CB.NearZ + 1e-3f);

	const float LogFarNear = logf(CB.FarZ / CB.NearZ);
	CB.LogZScale           = static_cast<float>(CB.ClusterCountZ) / LogFarNear;
	CB.LogZBias            = -logf(CB.NearZ) * CB.LogZScale;

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		ClusterGlobalConstantBuffer,
		&CB,
		sizeof(CB));
}

void FLightRenderFeature::UploadLocalLightBuffers(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	TArray<FLocalLightGPU>     LocalLightsGPU;
	TArray<FLightCullProxyGPU> ProxiesGPU;
	TArray<uint32>             ObjectLightIndicesGPU = SceneViewData.LightingInputs.ObjectLightIndices;

	if (NeedObejctLightIndexUpload(SceneViewData))
	{
		ObjectLightIndicesGPU = SceneViewData.LightingInputs.ObjectLightIndices;
	}

	LocalLightsGPU.reserve(SceneViewData.LightingInputs.LocalLights.size());
	ProxiesGPU.reserve(SceneViewData.LightingInputs.LocalLights.size());

	for (const FLocalLightRenderItem& Src : SceneViewData.LightingInputs.LocalLights)
	{
		if (LocalLightsGPU.size() >= LightListConfig::MaxLocalLights)
		{
			break;
		}

		FLocalLightGPU L = {};
		L.ColorIntensity = FVector4(Src.Color.X, Src.Color.Y, Src.Color.Z, Src.Intensity);
		L.PositionRange  = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, Src.Range);
		L.DirectionType  = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, static_cast<float>(Src.LightClass));
		L.AngleParams    = FVector4(Src.InnerAngleCos, Src.OuterAngleCos, Src.FalloffExponent, 0.0f);
		L.Axis0Extent    = FVector4(Src.Axis0.X, Src.Axis0.Y, Src.Axis0.Z, Src.Extent0);
		L.Axis1Extent    = FVector4(Src.Axis1.X, Src.Axis1.Y, Src.Axis1.Z, Src.Extent1);
		L.Axis2Extent    = FVector4(Src.Axis2.X, Src.Axis2.Y, Src.Axis2.Z, Src.Extent2);
		L.Flags          = Src.Flags;
		L.ShadowIndex    = Src.ShadowIndex;
		L.CookieIndex    = Src.CookieIndex;
		L.IESIndex       = Src.IESIndex;

		const uint32 LightIndex = static_cast<uint32>(LocalLightsGPU.size());
		LocalLightsGPU.push_back(L);

		FLightCullProxyGPU P = {};
		P.CullCenterRadius   = FVector4(Src.CullCenterWS.X, Src.CullCenterWS.Y, Src.CullCenterWS.Z, Src.CullRadius);
		P.PositionRange      = L.PositionRange;
		P.DirectionType      = L.DirectionType;
		P.AngleParams        = L.AngleParams;
		P.Axis0Extent        = L.Axis0Extent;
		P.Axis1Extent        = L.Axis1Extent;
		P.Axis2Extent        = L.Axis2Extent;
		P.Flags              = Src.Flags;
		P.LightIndex         = LightIndex;
		P.CullShapeType      = static_cast<uint32>(Src.CullShape);

		ProxiesGPU.push_back(P);
	}

	if (LocalLightsGPU.empty())
	{
		LocalLightsGPU.push_back(FLocalLightGPU {});
	}
	if (ProxiesGPU.empty())
	{
		ProxiesGPU.push_back(FLightCullProxyGPU {});
	}
	if (ObjectLightIndicesGPU.empty())
	{
		ObjectLightIndicesGPU.push_back(0u);
	}

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FLocalLightGPU),
		static_cast<uint32>(LocalLightsGPU.size()),
		LocalLightBuffer,
		LocalLightSRV);

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FLightCullProxyGPU),
		static_cast<uint32>(ProxiesGPU.size()),
		LightCullProxyBuffer,
		LightCullProxySRV);

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(uint32),
		static_cast<uint32>(ObjectLightIndicesGPU.size()),
		ObjectLightIndexBuffer,
		ObjectLightIndexSRV);

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		LocalLightBuffer,
		LocalLightsGPU.data(),
		sizeof(FLocalLightGPU) * LocalLightsGPU.size());

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		LightCullProxyBuffer,
		ProxiesGPU.data(),
		sizeof(FLightCullProxyGPU) * ProxiesGPU.size());

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		ObjectLightIndexBuffer,
		ObjectLightIndicesGPU.data(),
		sizeof(uint32) * ObjectLightIndicesGPU.size());

	const uint32 SceneLightCount  = static_cast<uint32>(SceneViewData.LightingInputs.LocalLights.size());
	const uint32 ActualLightCount = (std::min)(SceneLightCount, LightListConfig::MaxLocalLights);

	Stats.TotalSceneLights   = SceneLightCount;
	Stats.TotalLocalLights   = ActualLightCount;
	Stats.BudgetCulledLights = SceneLightCount > LightListConfig::MaxLocalLights
		                           ? SceneLightCount - LightListConfig::MaxLocalLights
		                           : 0u;
	Stats.LocalLightBufferBytes    = sizeof(FLocalLightGPU) * LocalLightsGPU.size();
	Stats.CullProxyBufferBytes     = sizeof(FLightCullProxyGPU) * ProxiesGPU.size();
	Stats.ClusterHeaderBufferBytes = sizeof(FLightClusterHeaderGPU) * (std::max)(1u, Stats.TotalClusters);
	Stats.ClusterIndexBufferBytes  = sizeof(uint32) * (std::max)(1u, Stats.TotalClusters) * LightListConfig::MaxLightsPerCluster;
	Stats.TotalBufferBytes         = Stats.LocalLightBufferBytes + Stats.CullProxyBufferBytes
			+ Stats.ClusterHeaderBufferBytes + Stats.ClusterIndexBufferBytes;
}

bool FLightRenderFeature::EnsureStagingBuffer(
	FRenderer&     Renderer,
	uint32         ElementStride,
	uint32         ElementCount,
	ID3D11Buffer*& Buffer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 ByteWidth = ElementStride * (std::max)(1u, ElementCount);

	if (Buffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);
		if (Desc.ByteWidth >= ByteWidth)
		{
			return true;
		}
		SafeRelease(Buffer);
	}

	D3D11_BUFFER_DESC Desc   = {};
	Desc.ByteWidth           = ByteWidth;
	Desc.Usage               = D3D11_USAGE_STAGING;
	Desc.BindFlags           = 0;
	Desc.CPUAccessFlags      = D3D11_CPU_ACCESS_READ;
	Desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = ElementStride;

	return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &Buffer));
}

bool FLightRenderFeature::EnsureDynamicStructuredBufferSRV(
	FRenderer&                 Renderer,
	uint32                     ElementStride,
	uint32                     ElementCount,
	ID3D11Buffer*&             Buffer,
	ID3D11ShaderResourceView*& SRV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 SafeCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth = ElementStride * SafeCount;

	bool bRecreate = !Buffer || !SRV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);

		if (Desc.ByteWidth < ByteWidth ||
			Desc.StructureByteStride != ElementStride ||
			Desc.Usage != D3D11_USAGE_DYNAMIC ||
			Desc.BindFlags != D3D11_BIND_SHADER_RESOURCE)
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

	D3D11_BUFFER_DESC Desc   = {};
	Desc.ByteWidth           = ByteWidth;
	Desc.Usage               = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	Desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &Buffer)))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)))
	{
		SafeRelease(Buffer);
		return false;
	}

	return true;
}

bool FLightRenderFeature::EnsureDefaultStructuredBufferSRVUAV(
	FRenderer&                  Renderer,
	uint32                      ElementStride,
	uint32                      ElementCount,
	ID3D11Buffer*&              Buffer,
	ID3D11ShaderResourceView*&  SRV,
	ID3D11UnorderedAccessView*& UAV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 SafeCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth = ElementStride * SafeCount;

	bool bRecreate = !Buffer || !SRV || !UAV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);

		if (Desc.ByteWidth < ByteWidth ||
			Desc.StructureByteStride != ElementStride ||
			Desc.Usage != D3D11_USAGE_DEFAULT ||
			Desc.BindFlags != (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(UAV);
	SafeRelease(SRV);
	SafeRelease(Buffer);

	D3D11_BUFFER_DESC Desc   = {};
	Desc.ByteWidth           = ByteWidth;
	Desc.Usage               = D3D11_USAGE_DEFAULT;
	Desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	Desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &Buffer)))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)))
	{
		SafeRelease(Buffer);
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format                           = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement              = 0;
	UAVDesc.Buffer.NumElements               = SafeCount;
	UAVDesc.Buffer.Flags                     = 0;

	if (FAILED(Device->CreateUnorderedAccessView(Buffer, &UAVDesc, &UAV)))
	{
		SafeRelease(SRV);
		SafeRelease(Buffer);
		return false;
	}

	return true;
}

bool FLightRenderFeature::EnsureTimingQueries(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	for (FLightTimingQuerySet& TimingQuerySet : TimingQuerySets)
	{
		if (!TimingQuerySet.Disjoint)
		{
			D3D11_QUERY_DESC Desc = {};
			Desc.Query            = D3D11_QUERY_TIMESTAMP_DISJOINT;
			if (FAILED(Device->CreateQuery(&Desc, &TimingQuerySet.Disjoint)))
			{
				return false;
			}
		}

		auto CreateTimestampQuery = [&Device](ID3D11Query*& Query) -> bool
		{
			if (Query)
			{
				return true;
			}

			D3D11_QUERY_DESC Desc = {};
			Desc.Query            = D3D11_QUERY_TIMESTAMP;
			return SUCCEEDED(Device->CreateQuery(&Desc, &Query));
		};

		if (!CreateTimestampQuery(TimingQuerySet.TileDepthBoundsStart)
			|| !CreateTimestampQuery(TimingQuerySet.TileDepthBoundsEnd)
			|| !CreateTimestampQuery(TimingQuerySet.LightCullingStart)
			|| !CreateTimestampQuery(TimingQuerySet.LightCullingEnd))
		{
			return false;
		}
	}

	return true;
}

void FLightRenderFeature::ResolveTimingQueries(ID3D11DeviceContext* DeviceContext)
{
	if (!DeviceContext)
	{
		return;
	}

	for (FLightTimingQuerySet& TimingQuerySet : TimingQuerySets)
	{
		if (!TimingQuerySet.bPending)
		{
			continue;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData = {};
		if (DeviceContext->GetData(TimingQuerySet.Disjoint, &DisjointData, sizeof(DisjointData), 0) != S_OK)
		{
			continue;
		}

		UINT64 TileDepthBoundsStart = 0u;
		UINT64 TileDepthBoundsEnd   = 0u;
		UINT64 LightCullingStart    = 0u;
		UINT64 LightCullingEnd      = 0u;

		if (DeviceContext->GetData(TimingQuerySet.TileDepthBoundsStart, &TileDepthBoundsStart, sizeof(TileDepthBoundsStart), 0) != S_OK
			|| DeviceContext->GetData(TimingQuerySet.TileDepthBoundsEnd, &TileDepthBoundsEnd, sizeof(TileDepthBoundsEnd), 0) != S_OK
			|| DeviceContext->GetData(TimingQuerySet.LightCullingStart, &LightCullingStart, sizeof(LightCullingStart), 0) != S_OK
			|| DeviceContext->GetData(TimingQuerySet.LightCullingEnd, &LightCullingEnd, sizeof(LightCullingEnd), 0) != S_OK)
		{
			continue;
		}

		TimingQuerySet.bPending = false;

		if (DisjointData.Disjoint || DisjointData.Frequency == 0u)
		{
			continue;
		}

		Stats.TileDepthBoundsPassTimeMs = TicksToMilliseconds(TileDepthBoundsStart, TileDepthBoundsEnd, DisjointData.Frequency);
		Stats.LightCullingPassTimeMs    = TicksToMilliseconds(LightCullingStart, LightCullingEnd, DisjointData.Frequency);
		Stats.TotalCullingTimeMs        = Stats.TileDepthBoundsPassTimeMs + Stats.LightCullingPassTimeMs;
	}
}
