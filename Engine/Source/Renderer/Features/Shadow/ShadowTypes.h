#pragma once

#include <d3d11.h>
#include "Renderer/Features/Lighting/LightTypes.h"
#include "CoreMinimal.h"

class AActor;

namespace ShadowConfig
{
	static constexpr uint32 MaxShadowLights = 16;


	static constexpr uint32 MaxSpotShadowViews		   = 8;
	static constexpr uint32 MaxPointShadowCubes        = 4;
	static constexpr uint32 MaxShadowViews = MaxSpotShadowViews + MaxPointShadowCubes * 6;
	static constexpr uint32 MaxDirCascade = 4;

	static constexpr uint32 PointShadowSliceOffset = MaxSpotShadowViews;

	static constexpr uint32 DefaultShadowMapResolution = 512;
	static constexpr uint32 MaxLocalShadowMapResolution = 2048;
	static constexpr uint32 MinShadowMapResolution     = 64;
	static constexpr uint32 MaxShadowMapResolution     = 4096;
	static constexpr uint32 DirShadowDepthResolution = 4096;
	static constexpr uint32 DirMaxShadowDepthResolution = 8192;
	static constexpr float  DefaultNearZ               = 0.05f;
}

namespace ShadowSlots
{
	static constexpr uint32 ShadowLightSRV      = 20;
	static constexpr uint32 ShadowViewSRV       = 21;
	static constexpr uint32 ShadowMapSRV        = 22;
	static constexpr uint32 ShadowMomentsSRV    = 23;

	static constexpr uint32 ShadowCubeSRV		= 24;
	static constexpr uint32 ShadowMomentCubeSRV = 25;

	static constexpr uint32 DirShadowLightSRV = 26;
	static constexpr uint32 DirShadowViewSRV = 27;
	static constexpr uint32 DirShadowMapSRV = 28;
	static constexpr uint32 DirShadowMomentsSRV = 29;

	static constexpr uint32 ShadowSampler       = 8;
	static constexpr uint32 ShadowLinearSampler = 9;
}

enum class EShadowFilterMode : uint32
{
	Raw = 0u,
	PCF = 1u,
	VSM = 2u,
};

enum class EShadowLightType : uint32
{
	Directional = 0,
	Spot        = 1,
	Point       = 2,
	Count
};

enum class EShadowProjectionType : uint32
{
	Orthographic = 0,
	Perspective  = 1,
};

struct FShadowLightRenderItem
{
	EShadowLightType LightType        = EShadowLightType::Spot;
	uint32           SourceLightIndex = UINT32_MAX;

	uint32 ShadowIndex = UINT32_MAX;

	uint32 FirstViewIndex = UINT32_MAX;
	uint32 ViewCount      = 0;

	float Bias       = 0.001f;
	float SlopeBias  = 0.001f;
	float NormalBias = 0.0f;
	float Sharpen    = 0.0f;

	uint32 CubeArrayIndex = UINT32_MAX;
	ELightMobility Mobility = ELightMobility::Movable;

	FVector PositionWS  = FVector::ZeroVector;
	FVector DirectionWS = FVector(1.0f, 0.0f, 0.0f);

	FVector4 Params0 = FVector4(0, 0, 0, 0);
	FVector4 Params1 = FVector4(0, 0, 0, 0);

	bool bCacheDirty = true;
};

struct FShadowViewRenderItem
{
	uint32 ShadowLightIndex = UINT32_MAX;
	uint32 ArraySlice       = UINT32_MAX;

	EShadowProjectionType ProjectionType = EShadowProjectionType::Perspective;

	EShadowLightType LightType = EShadowLightType::Spot;

	FMatrix View           = FMatrix::Identity;
	FMatrix Projection     = FMatrix::Identity;
	FMatrix ViewProjection = FMatrix::Identity;

	FVector PositionWS = FVector::ZeroVector;

	float NearZ = ShadowConfig::DefaultNearZ;
	float FarZ  = 1000.0f;

	bool			  bAtlasAllocated     = false;
	uint32			  AllocatedResolution = 0;
	uint32            RequestedResolution = 0;
	float			  ShadowResolutionScale = 1.0f;
	EShadowFilterMode FilterMode          = EShadowFilterMode::VSM;

	FVector AtlasUV = FVector::ZeroVector;

	D3D11_VIEWPORT Viewport = {};

	FVector4 BiasParams = FVector4(0, 0, 0, 0);

	AActor* SourceActor = nullptr;
};


struct FShadowLightGPU
{
	uint32 LightType = 0;
	uint32 FirstViewIndex = 0;
	uint32 ViewCount = 0;
	uint32 Flags = 0;

	FVector4 PositionType;

	FVector4 DirectionBias;

	FVector4 Params0;
};


struct FShadowViewGPU
{
	FMatrix LightViewProjection;

	uint32 ArraySlice = 0;
	uint32 ProjectionType = 0;
	uint32 FilterMode = 0;
	uint32 Pad0 = 0;

	FVector4 ViewParams;
	FVector4 BiasParams;
	
	FVector AtlasUV; // X,Y: UV offset, Z: UV scale
	float   Pad1 = 0.0f;
};
