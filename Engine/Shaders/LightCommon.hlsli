#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

#ifndef ENABLE_SHADOWS
#define ENABLE_SHADOWS 0
#endif

#define LIGHT_CLASS_POINT 0
#define LIGHT_CLASS_SPOT 1
#define LIGHT_CLASS_RECT 2
#define LIGHT_CLASS_TUBE 3
#define LIGHT_CLASS_CUSTOM 4

#define CULL_SHAPE_SPHERE 0
#define CULL_SHAPE_CONE 1
#define CULL_SHAPE_OBB 2
#define CULL_SHAPE_CAPSULE 3
#define CULL_SHAPE_CUSTOM 4

#define MAX_LOCAL_LIGHTS 1024
#define MAX_LIGHTS_PER_CLUSTER 1024
#define LIGHT_VISUALIZATION_NONE 0
#define LIGHT_VISUALIZATION_CLUSTER_HEATMAP 1
#define LIGHT_VISUALIZATION_CSM_CASCADE 2

#define INVALID_SHADOW_INDEX 0xFFFFFFFFu

#define SHADOW_LIGHT_DIRECTIONAL 0
#define SHADOW_LIGHT_SPOT        1
#define SHADOW_LIGHT_POINT       2

#define SHADOW_PROJECTION_ORTHOGRAPHIC 0
#define SHADOW_PROJECTION_PERSPECTIVE  1

struct FAmbientLightInfo
{
	float4 ColorIntensity;
};

struct FDirectionalLightInfo
{
	float4 ColorIntensity;
	float4 DirectionEtc;
	float4 CascadeSplits;
};

cbuffer MaterialData : register(b2)
{
	float4 ColorTint;
	float2 UVScrollSpeed;
	float2 Padding0;
	float4 EmissiveColor;
	float Shininess;
	float3 Padding1;
};

cbuffer Lighting : register(b4)
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	uint AmbientEnabled;
	uint DirectionalLightCount;
	uint LightingPad0;
	uint LightingPad1;
};

struct FLocalLightGPU
{
	float4 ColorIntensity;
	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;

	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;

	uint Flags;
	uint ShadowIndex;
	uint CookieIndex;
	uint IESIndex;
};

struct FLightCullProxyGPU
{
	float4 CullCenterRadius;

	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;

	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;

	uint Flags;
	uint LightIndex;
	uint CullShapeType;
	uint Reserved;
};

struct FLightClusterHeader
{
	uint Offset;
	uint Count;
	uint RawCount;
	uint Pad1;
};

struct FTileDepthBoundsGPU
{
	float MinViewZ;
	float MaxViewZ;
	uint TileMinSlice;
	uint TileMaxSlice;
	uint HasGeometry;
	uint Pad0;
	uint Pad1;
	uint Pad2;
};

cbuffer LightClusterGlobals : register(b8)
{
	float4x4 ClusterView;
	float4x4 ClusterProjection;
	float4x4 ClusterInverseProjection;
	float4x4 ClusterInverseView;

	float4 ClusterCameraPosition;
	float4 ScreenParams;

	uint ClusterCountX;
	uint ClusterCountY;
	uint ClusterCountZ;
	uint LocalLightCount;

	uint OrthographicView;
	uint RuntimeMaxLightsPerCluster;
	uint LightingEnabled;
	uint VisualizationMode;

	float NearZ;
	float FarZ;
	float LogZScale;
	float LogZBias;
};

bool IsOrthographicClusterView()
{
	return OrthographicView != 0u;
}

float LinearizeDeviceDepth(float deviceDepth)
{
	deviceDepth = saturate(deviceDepth);

	if (IsOrthographicClusterView())
	{
		return lerp(NearZ, FarZ, deviceDepth);
	}

	return (NearZ * FarZ) / max(FarZ - deviceDepth * (FarZ - NearZ), 1.0e-6f);
}

float ViewDepthToDeviceDepth(float viewDepth)
{
	float clampedViewDepth = clamp(viewDepth, NearZ, FarZ);

	if (IsOrthographicClusterView())
	{
		return saturate((clampedViewDepth - NearZ) / max(FarZ - NearZ, 1.0e-6f));
	}

	return saturate((FarZ - (NearZ * FarZ) / max(clampedViewDepth, NearZ)) / max(FarZ - NearZ, 1.0e-6f));
}

float3 ApplyCSMDebugOverlay(float viewDepth, float4 splits, float3 baseColor)
{
    uint cascadeIndex = 0;
	
    if (viewDepth > splits.x) cascadeIndex = 1;
    if (viewDepth > splits.y) cascadeIndex = 2;
    if (viewDepth > splits.z) cascadeIndex = 3;
	
    float3 debugColors[4] =
    {
        float3(1.0f, 0.2f, 0.2f),
        float3(0.2f, 1.0f, 0.2f),
        float3(0.2f, 0.2f, 1.0f),
        float3(1.0f, 1.0f, 0.2f) 
    };
    return baseColor * 0.2f + debugColors[cascadeIndex] * 0.8f;
}

StructuredBuffer<FLightClusterHeader> ClusterLightHeaders : register(t10);
StructuredBuffer<uint>                ClusterLightIndices : register(t11);
StructuredBuffer<FLocalLightGPU>      LocalLights         : register(t12);
StructuredBuffer<uint>                ObjectLightIndices  : register(t13);

#if ENABLE_SHADOWS

struct FShadowLightGPU
{
	uint LightType;
	uint FirstViewIndex;
	uint ViewCount;
	uint Flags;

	float4 PositionType;
	float4 DirectionBias;
	float4 Params0;
};

struct FShadowViewGPU
{
	float4x4 LightViewProjection;

	uint ArraySlice;
	uint ProjectionType;
	uint FilterMode;
	uint Pad0;

	float4 ViewParams;
    float4 BiasParams;

	float3 AtlasUV;
	float Pad1;
};

StructuredBuffer<FShadowLightGPU> ShadowLights       : register(t20);
StructuredBuffer<FShadowViewGPU>  ShadowViews        : register(t21);

Texture2D<float>             ShadowDepth   : register(t22); // PCF
Texture2D<float2>            ShadowMomentsTexture : register(t23); // VSM
TextureCubeArray<float>		     ShadowDepthCubeArray: register(t24);// Cube Map
TextureCubeArray<float2>		     ShadowMomentsCubeArray: register(t25);// Cube Map VSM

StructuredBuffer<FShadowLightGPU>	DirShadowLights : register(t26);
StructuredBuffer<FShadowViewGPU>	DirShadowViews : register(t27);

Texture2D<float>				DirShadowDepthTexture		: register(t28); // PCF
Texture2D<float2>				DirShadowMomentsTexture		: register(t29); // VSM

SamplerComparisonState            ShadowSampler      : register(s8); // PCF
SamplerState                      LinearClampSampler : register(s9); // VSM

struct FAtlasTile
{
	float2 Offset;
	float Scale;
	float TexelSize;
};

float2 ToAtlasUV(float2 localUV, FAtlasTile tile)
{
	return localUV * tile.Scale + tile.Offset;
}

float2 ClampAtlasUV(float2 atlasUV, FAtlasTile tile)
{
	float2 minUV = tile.Offset + tile.TexelSize * 0.5f;
	float2 maxUV = tile.Offset + tile.Scale - tile.TexelSize * 0.5f;
	return clamp(atlasUV, minUV, maxUV);
}

FAtlasTile GetAtlasTile(FShadowViewGPU view)
{
	FAtlasTile tile;
	float atlasSize = max(view.ViewParams.z, 1.0e-6f);

	tile.Offset = view.AtlasUV.xy / atlasSize;
	tile.Scale = view.AtlasUV.z / atlasSize;
	tile.TexelSize = view.ViewParams.w;

	return tile;
}

float ComputeShadowBias(
	FShadowLightGPU shadowLight,
	float3 N,
	float3 L)
{
	float baseBias  = shadowLight.DirectionBias.w;
	float slopeBias = shadowLight.Params0.x;

	float ndotl = saturate(dot(N, L));
	return baseBias + slopeBias * (1.0f - ndotl);
}

bool ComputeShadowCoords(
	FShadowViewGPU shadowView,
	float3 worldPos,
	out float2 uv,
	out float compareDepth)
{
	float4 clip = mul(float4(worldPos, 1.0f), shadowView.LightViewProjection);

	if (clip.w <= 0.0f)
	{
		uv = 0.0f.xx;
		compareDepth = 0.0f;
		return false;
	}

	float3 ndc = clip.xyz / clip.w;

	if (ndc.x < -1.0f || ndc.x > 1.0f ||
		ndc.y < -1.0f || ndc.y > 1.0f ||
		ndc.z <  0.0f || ndc.z > 1.0f)
	{
		uv = 0.0f.xx;
		compareDepth = 0.0f;
		return false;
	}

	uv.x = ndc.x * 0.5f + 0.5f;
	uv.y = -ndc.y * 0.5f + 0.5f;

	compareDepth = saturate(ndc.z);
	return true;
}



float SampleShadowViewPCF(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}
	
	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);

	FAtlasTile tile = GetAtlasTile(shadowView);
	uv = ToAtlasUV(uv, tile);
	uv = ClampAtlasUV(uv, tile);

	float visibility = 0.0f;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 tapUV = uv + float2(x, y) * tile.TexelSize;

			tapUV = ClampAtlasUV(tapUV, tile);
			visibility += ShadowDepth.SampleCmpLevelZero(
				ShadowSampler,
				tapUV,
				compareDepth);
		}
	}

	return visibility / 9.0f;
}
float SampleShadowViewPoint(FShadowLightGPU shadowLight, float3 worldPos, float3 N, float3 L) // TODO Point
{
	uint cubeIndex = (uint) shadowLight.Params0.w;
	float3 lightPos = shadowLight.PositionType.xyz;
	float3 lightToSurface = worldPos - lightPos;
	// 면판정 D3D 큐브 표준 순서와 일치해야함
	float3 absDir = abs(lightToSurface);
	uint faceIndex = 0;
	if (absDir.x >= absDir.y && absDir.x >= absDir.z)
		faceIndex = (lightToSurface.x > 0) ? 0 : 1;
	else if (absDir.y >= absDir.z)
		faceIndex = (lightToSurface.y > 0) ? 2 : 3;
	else
		faceIndex = (lightToSurface.z > 0) ? 4 : 5;
	// 그면의 ViewProjection으로  NDC 깊이 계산
	FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex + faceIndex];
	float4 shadowPos = mul(float4(worldPos, 1.0f), view.LightViewProjection);
	shadowPos.xyz /= shadowPos.w;

	if (shadowPos.w <= 0 || shadowPos.z < 0 || shadowPos.z > 1)
		return 1.0f;
	float bias = shadowLight.DirectionBias.w;
	float rawDepth = ShadowDepthCubeArray.SampleLevel(LinearClampSampler,float4(lightToSurface, (float) cubeIndex),0.0f).r;
	
	return (shadowPos.z - bias <= rawDepth) ? 1.0f : 0.0f;
}
float ReduceLightBleeding(float pMax, float amount)
{
	return saturate((pMax - amount) / max(1.0f - amount, 1.0e-5f));
}
float SampleShadowViewPointPCF(FShadowLightGPU shadowLight, float3 worldPos, float3 N, float3 L) // 혹은 svPosition 포함된 버전
{
	 float3 L_dir = normalize(shadowLight.PositionType.xyz - worldPos);

	float normalBiasMultiplier = 0.1f;

	float slopeScale = 1.0f - saturate(dot(N, L_dir));
	float3 biasedWorldPos = worldPos + N * (normalBiasMultiplier * slopeScale);


	uint cubeIndex = (uint)shadowLight.Params0.w;
	float3 lightToSurface = biasedWorldPos - shadowLight.PositionType.xyz;


    float3 absDir = abs(lightToSurface);
    uint faceIndex = 0;
    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
        faceIndex = (lightToSurface.x > 0) ? 0 : 1;
    else if (absDir.y >= absDir.z)
        faceIndex = (lightToSurface.y > 0) ? 2 : 3;
    else
        faceIndex = (lightToSurface.z > 0) ? 4 : 5;

    FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex + faceIndex];
    float4 clip = mul(float4(biasedWorldPos, 1.0f), view.LightViewProjection); // 여기도 biasedWorldPos 사용
    if (clip.w <= 0.0f) return 1.0f;

    float compareDepth = saturate(clip.z / clip.w);
    float bias = ComputeShadowBias(shadowLight, N, L);
    compareDepth = saturate(compareDepth - bias);


    // 1. 월드 좌표 기반의 의사 난수(Pseudo-random) 노이즈 생성 (0.0 ~ 1.0)
    float noise = frac(sin(dot(worldPos, float3(12.9898f, 78.233f, 37.719f))) * 43758.5453f);
    
    // 2. 빛 방향을 기준으로 회전할 수직 기저 벡터(Tangent, Bitangent) 생성
    float3 L_norm = normalize(lightToSurface);
    float3 up = abs(L_norm.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, L_norm));
    float3 bitangent = cross(L_norm, tangent);

    // 3. 노이즈 값을 라디안(Angle)으로 변환하여 2D 회전 행렬 생성
    float s, c;
    sincos(noise * 6.2831853f, s, c); // 2 * PI
    float2x2 rotationMatrix = float2x2(c, -s, s, c);

    // 4. 2D Poisson Disk 샘플 (8개로 최적화)
    const float2 poissonDisk[8] = {
        float2(-0.8406283f,  0.4287514f),
        float2(-0.4709848f, -0.6695281f),
        float2( 0.0076249f, -0.1583569f),
        float2( 0.1691238f,  0.8679075f),
        float2( 0.6974795f, -0.6094580f),
        float2( 0.8427953f,  0.3013238f),
        float2(-0.7634493f, -0.1873136f),
        float2( 0.3800631f,  0.2238478f)
    };

    float dist = length(lightToSurface);
    float offsetScale = dist * 0.005f; // 그림자 번짐 정도 (필요에 따라 조절)

    float visibility = 0.0f;
    
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        // 디스크의 2D 좌표를 무작위로 회전
        float2 rotatedOffset = mul(rotationMatrix, poissonDisk[i]);
        
        // 회전된 2D 오프셋을 3D 공간의 탄젠트 평면에 매핑하여 최종 샘플 방향 계산
        float3 sampleDir = lightToSurface + (tangent * rotatedOffset.x + bitangent * rotatedOffset.y) * offsetScale;
        
        visibility += ShadowDepthCubeArray.SampleCmpLevelZero(
            ShadowSampler,
            float4(sampleDir, (float)cubeIndex),
            compareDepth);
    }
    
    return visibility / 8.0f;
}
float SampleShadowViewPointVSM(FShadowLightGPU shadowLight, float3 worldPos, float3 N, float3 L) // TODO Point
{
	uint cubeIndex = (uint) shadowLight.Params0.w;
	float3 lightPos = shadowLight.PositionType.xyz;
	float3 lightToSurface = worldPos - lightPos;
	// 면판정 D3D 큐브 표준 순서와 일치해야함
	float3 absDir = abs(lightToSurface);
	uint faceIndex = 0;
	if (absDir.x >= absDir.y && absDir.x >= absDir.z)
		faceIndex = (lightToSurface.x > 0) ? 0 : 1;
	else if (absDir.y >= absDir.z)
		faceIndex = (lightToSurface.y > 0) ? 2 : 3;
	else
		faceIndex = (lightToSurface.z > 0) ? 4 : 5;

	// 그면의 ViewProjection으로  NDC 깊이 계산
	FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex + faceIndex];
	float4 clip = mul(float4(worldPos, 1.0f), view.LightViewProjection);
    if (clip.w <= 0.0f) 
		return 1.0f;
	
    float compareDepth = saturate(clip.z / clip.w);
    float bias = ComputeShadowBias(shadowLight, N, L);
    compareDepth = saturate(compareDepth - bias);
	
	float2 moments = ShadowMomentsCubeArray.SampleLevel(LinearClampSampler, float4(lightToSurface, (float)cubeIndex), 0.0f).rg;

	// Chebyshev (2D VSM과 동일)
	float mean = moments.x;
    float Variance = moments.y - (moments.x * moments.x);
	Variance = max(Variance, 1.0e-6f);
	if (compareDepth <= mean)
		return 1.0f;
	
	float d = compareDepth - mean;
	float pMax = Variance / (Variance + d * d);
	pMax = ReduceLightBleeding(pMax, shadowLight.Params0.z);
    return saturate(pMax);
}


float SampleShadowViewVSM(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}
	
	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);

	FAtlasTile tile = GetAtlasTile(shadowView);
	uv = ToAtlasUV(uv, tile);
	uv = ClampAtlasUV(uv, tile);

	float2 moments = ShadowMomentsTexture.SampleLevel(
		LinearClampSampler,
		uv,
		0.0f
	).rg;

	float mean     = moments.x;
	float variance = moments.y - mean * mean;
	variance       = max(variance, 1.0e-6f);
	if (compareDepth <= mean)
	{
		return 1.0f;
	}

	float d    = compareDepth - mean;
	float pMax = variance / (variance + d * d);
	pMax       = ReduceLightBleeding(pMax, shadowLight.Params0.z);
	return saturate(pMax);
}

float SampleShadowViewRawDepth(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}

	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);

	FAtlasTile tile = GetAtlasTile(shadowView);
	float2 baseUV = ToAtlasUV(uv, tile);
	baseUV = ClampAtlasUV(baseUV, tile);

	float rawDepth = ShadowDepth.SampleLevel(
		LinearClampSampler,
		baseUV,
		0.0f
	).r;

	return (compareDepth <= rawDepth) ? 1.0f : 0.0f;
}


float GetCascadeVisibility(FShadowViewGPU view, FShadowLightGPU shadowLight, float3 worldPos, float3 N, float3 L)
{
	float4 clip = mul(float4(worldPos, 1.0f), view.LightViewProjection);
    if (clip.w <= 0.0f)
        return 1.0f;

    float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f ||
        ndc.y < -1.0f || ndc.y > 1.0f ||
        ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }
    
    float2 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = -ndc.y * 0.5f + 0.5f;
    
    float baseBias = view.BiasParams.x;
    float slopeBias = view.BiasParams.y;
    float NdotL = saturate(dot(N, L));
    float slope = sqrt(saturate(1.0f - NdotL * NdotL)) / max(NdotL, 0.0001f);
    // float variableBias = clamp(slopeBias * slope, 0.0f, baseBias * 10.0f);	clamp로 slope 막기
	float variableBias = min(slopeBias * slope, 0.05f);
    float compareDepth = saturate(ndc.z) - (baseBias + variableBias);

	FAtlasTile tile = GetAtlasTile(view);
	float2 baseUV = ToAtlasUV(uv, tile);
	baseUV = ClampAtlasUV(baseUV, tile);

    // Filter Mode에 따른 분기 (0: Raw, 1: PCF, 2: VSM) 
    if (view.FilterMode == 1u) // PCF
    {
        float visibility = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
				float2 tapUV = baseUV + float2(x, y) * tile.TexelSize;

                visibility += DirShadowDepthTexture.SampleCmpLevelZero(
					ShadowSampler, 
					tapUV, 
					compareDepth);
            }
        }
        return visibility / 9.0f;
    }
    else if (view.FilterMode == 2u) // VSM
    {
        float2 moments = DirShadowMomentsTexture.SampleLevel(
					LinearClampSampler, 
					baseUV,
					0.0f).rg;
        float mean = moments.x;
        float variance = max(moments.y - mean * mean, 1.0e-6f);
        
        if (compareDepth <= mean)
            return 1.0f;
        
        float d = compareDepth - mean;
        float pMax = variance / (variance + d * d);
        return saturate(ReduceLightBleeding(pMax, shadowLight.Params0.z));
    }

    // FilterMode == 0u (Raw Depth)
    float rawDepth = DirShadowDepthTexture.SampleLevel(LinearClampSampler, baseUV, 0.0f).r;
    return (compareDepth <= rawDepth) ? 1.0f : 0.0f;
}

float EvaluateDirectionalShadow(uint shadowIndex, float3 worldPos, float3 N, float3 L, float viewDepth)
{
    if (shadowIndex == INVALID_SHADOW_INDEX) return 1.0f;
    
    FShadowLightGPU shadowLight = DirShadowLights[shadowIndex];
    if (shadowLight.ViewCount == 0u) return 1.0f;
    
    // Cascade 인덱스 판별
    float4 splits = Directional.CascadeSplits;
    uint cascadeIndex = 0;
    if (viewDepth > splits.x) cascadeIndex = 1;
    if (viewDepth > splits.y) cascadeIndex = 2;
    if (viewDepth > splits.z) cascadeIndex = 3;
    cascadeIndex = min(cascadeIndex, shadowLight.ViewCount - 1);

    // 현재 Cascade의 전체 길이 계산 및 동적 Blend Band 설정
    float currentSplit = (cascadeIndex == 0) ? splits.x :
                         (cascadeIndex == 1) ? splits.y :
                         (cascadeIndex == 2) ? splits.z : splits.w;

    float prevSplit = (cascadeIndex == 0) ? 0.0f :
                      (cascadeIndex == 1) ? splits.x :
                      (cascadeIndex == 2) ? splits.y : splits.z;
                      
    float cascadeLength = currentSplit - prevSplit;
    
    // 15% 구간에서만 부드럽게 섞이도록 설정
    float blendBand = cascadeLength * 0.15f; 
    uint nextCascadeIndex = cascadeIndex;
    float blendWeight = 0.0f;

    if (cascadeIndex < shadowLight.ViewCount - 1)
    {
        float distToSplit = currentSplit - viewDepth;
        if (distToSplit > 0.0f && distToSplit < blendBand)
        {
            blendWeight = smoothstep(blendBand, 0.0f, distToSplit);
            nextCascadeIndex = cascadeIndex + 1;
        }
    }


    FShadowViewGPU view = DirShadowViews[shadowLight.FirstViewIndex + cascadeIndex];
    float visibility = GetCascadeVisibility(view, shadowLight, worldPos, N, L);
    
    if (blendWeight > 0.0f)
    {
        FShadowViewGPU nextView = DirShadowViews[shadowLight.FirstViewIndex + nextCascadeIndex];
        float nextVisibility = GetCascadeVisibility(nextView, shadowLight, worldPos, N, L);
        
        visibility = lerp(visibility, nextVisibility, blendWeight);
    }

    return visibility;
}


/*float3 DebugDirectionalShadow(uint shadowIndex, float3 worldPos, float viewDepth)
{
    if (shadowIndex == INVALID_SHADOW_INDEX)
        return float3(1, 0, 0); // 🔴 빨간색: 빛 정보 바인딩 안됨
    
    FShadowLightGPU shadowLight = DirShadowLights[shadowIndex];
    if (shadowLight.ViewCount == 0u)
        return float3(1, 0, 0); // 🔴 빨간색: 뷰 카운트 0

    uint cascadeIndex = 0;
    if (viewDepth > Directional.CascadeSplits.x)
        cascadeIndex = 1;
    if (viewDepth > Directional.CascadeSplits.y)
        cascadeIndex = 2;
    if (viewDepth > Directional.CascadeSplits.z)
        cascadeIndex = 3;
    cascadeIndex = min(cascadeIndex, shadowLight.ViewCount - 1);

    FShadowViewGPU view = DirShadowViews[shadowLight.FirstViewIndex + cascadeIndex];

    float4 clip = mul(float4(worldPos, 1.0f), view.LightViewProjection);
    if (clip.w <= 0.0f)
        return float3(1, 1, 0); // 🟡 노란색: W 나누기 오류

    float3 ndc = clip.xyz / clip.w;

    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) 
        return float3(0, 1, 0); // 🟢 초록색: 투영 영역 밖 (직교 투영 Width/Height 설정 문제)
        
    if (ndc.z < 0.0f || ndc.z > 1.0f) 
        return float3(0, 0, 1); // 🔵 파란색: 깊이 영역 밖 (직교 투영 Near/Far 설정 문제)

    float2 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = -ndc.y * 0.5f + 0.5f;

    // 정상 범위 안에 있다면, GPU 텍스처에 찍혀있는 깊이 값(Raw Depth)을 그대로 가져와서 화면에 출력합니다.
    float rawDepth = DirShadowDepthTexture.SampleLevel(LinearClampSampler, uv, 0.0f).r;
    
    return float3(rawDepth, rawDepth, rawDepth); // ⚪⚫ 흑백: 텍스처 내부 데이터 출력
}*/

float EvaluateSpotShadow(
	FShadowLightGPU shadowLight,
	float3 worldPos,
	float3 N,
	float3 L)
{
	if (shadowLight.ViewCount == 0u)
	{
		return 1.0f;
	}

	FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex];
	if (view.FilterMode == 0u) // Raw
	{
		return SampleShadowViewRawDepth(shadowLight, view, worldPos, N, L);
	}
	if (view.FilterMode == 1u) // PCF
	{
		return SampleShadowViewPCF(shadowLight, view, worldPos, N, L);
	}
	else // VSM
	{
		return SampleShadowViewVSM(shadowLight, view, worldPos, N, L);
	}
}

float EvaluateShadow(
	uint shadowIndex,
	uint lightClass,
	float3 worldPos,
	float3 N,
	float3 L)
{
	if (shadowIndex == INVALID_SHADOW_INDEX)
	{
		return 1.0f;
	}

	FShadowLightGPU shadowLight = ShadowLights[shadowIndex];

	if (shadowLight.LightType == SHADOW_LIGHT_SPOT && lightClass == LIGHT_CLASS_SPOT)
	{
		return EvaluateSpotShadow(shadowLight, worldPos, N, L);
	}
	if (shadowLight.LightType == SHADOW_LIGHT_POINT && lightClass == LIGHT_CLASS_POINT)
	{
		if (shadowLight.ViewCount == 0u) return 1.0f;
    
		FShadowViewGPU firstView = ShadowViews[shadowLight.FirstViewIndex];
		if (firstView.FilterMode == 2u)  // VSM
			return SampleShadowViewPointVSM(shadowLight, worldPos, N, L);
		else if (firstView.FilterMode == 1u)
			return SampleShadowViewPointPCF(shadowLight, worldPos, N, L);
		else
			return SampleShadowViewPoint(shadowLight, worldPos, N, L);
	} 
	// TODO :  Directional
	return 1.0f;
}



#else


float EvaluateShadow(
	uint shadowIndex,
	uint lightClass,
	float3 worldPos,
	float3 N,
	float3 L)
{
	return 1.0f;
}

float EvaluateDirectionalShadow(
    uint shadowIndex,
    float3 worldPos,
    float3 N,
    float3 L,
    float viewDepth)
{
    return 0.0f;
}

#endif

float CalculateAttenuation(float distance, float range)
{
	float safeRange = max(range, 1.0e-4f);
	float d = max(distance, 0.0f);
	float distanceSq = d * d;
	float invRangeSq = 1.0f / (safeRange * safeRange);

	// UE ?????radius window: (1 - (d/r)^4)^2
	float x = distanceSq * invRangeSq; // (d/r)^2
	float rangeMask = saturate(1.0f - x * x);
	rangeMask *= rangeMask;

	// 域뱀눊援끿뵳??⑥눖?귞빊?獄쎻뫗?
	const float MinDistanceSq = 0.25f;
	float inverseSquare = 1.0f / max(distanceSq, MinDistanceSq);

	return rangeMask * inverseSquare;
}

float3 BuildFallbackTangent(float3 normal)
{
	float3 referenceAxis = (abs(normal.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
	return normalize(cross(referenceAxis, normal));
}

void ReOrthonormalizeTBN(
	float3 inputNormal,
	float3 inputTangent,
	float3 inputBitangent,
	out float3 outNormal,
	out float3 outTangent,
	out float3 outBitangent)
{
	outNormal = normalize(inputNormal);

	float3 orthogonalTangent = inputTangent - outNormal * dot(inputTangent, outNormal);
	float tangentLengthSq = dot(orthogonalTangent, orthogonalTangent);
	if (tangentLengthSq > 1.0e-8f)
	{
		outTangent = orthogonalTangent * rsqrt(tangentLengthSq);
	}
	else
	{
		outTangent = BuildFallbackTangent(outNormal);
	}

	float handedness = (dot(cross(outNormal, outTangent), inputBitangent) < 0.0f) ? -1.0f : 1.0f;
	outBitangent = normalize(cross(outNormal, outTangent)) * handedness;
}

#if HAS_NORMAL_MAP
float3 GetNormalFromMap(
	Texture2D normalMapTexture,
	SamplerState normalMapSampler,
	float3 vertexNormal,
	float3 tangent, 
	float3 bitangent,
	float2 uv)
{
	float3 tangentNormal = normalMapTexture.Sample(normalMapSampler, uv).rgb * 2.0f - 1.0f;

	float3 T;
	float3 B;
	float3 N;
	ReOrthonormalizeTBN(vertexNormal, tangent, bitangent, N, T, B);
	float3x3 TBN = float3x3(T, B, N);

	return normalize(mul(tangentNormal, TBN));
}
#endif

float4 CalculateAmbientLight(FAmbientLightInfo info)
{
	return float4(info.ColorIntensity.xyz * info.ColorIntensity.w, 1.0f);
}

float4 CalculateDirectionalLight(FDirectionalLightInfo info,
                                 float3 worldPos, float3 N, float3 V)
{
	float3 L = normalize(-info.DirectionEtc.xyz);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
    float spec = (diff > 0.0f) ? pow(max(0.0f, dot(N, H)), Shininess) : 0.0f;

    float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff;
    float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec;

    return float4(diffuse + specular, 1.0f);
}

float4 CalculatePointLight(FLocalLightGPU info,
                           float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

    float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation;
    float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation;

    return float4(diffuse + specular, 1.0f);
}

float4 CalculateSpotLight(FLocalLightGPU info,
                          float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float theta = dot(L, normalize(-info.DirectionType.xyz));
	float innerCutoff = info.AngleParams.x;
	float outerCutoff = info.AngleParams.y;
	float intensity = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));

	if (intensity <= 0.0f)
		return float4(0, 0, 0, 0);

	float diff = max(0.0f, dot(N, L));
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

	float shadow = EvaluateShadow(info.ShadowIndex, LIGHT_CLASS_SPOT, worldPos, N, L);

	float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation * intensity * shadow;
	float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation * intensity * shadow;

	return float4(diffuse + specular, 1.0f);
}

float4 ComputeLocalLight(FLocalLightGPU light, float3 worldPos, float3 N, float3 V)
{
	uint lightClass = (uint)light.DirectionType.w;

	switch (lightClass)
	{
	case LIGHT_CLASS_POINT: return CalculatePointLight(light, worldPos, N, V);
	case LIGHT_CLASS_SPOT: return CalculateSpotLight(light, worldPos, N, V);
	default: return 0.0f.xxxx;
	}
}

void ComputeLocalLightContributions(
	FLocalLightGPU light,
	float3 worldPos,
	float3 N,
	float3 V,
	bool applyShadow,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	float3 toLight = light.PositionRange.xyz - worldPos;
	float distance = length(toLight);
	if (distance > light.PositionRange.w)
	{
		return;
	}

	float3 L = toLight / max(distance, 1.0e-5f);
	float attenuation = CalculateAttenuation(distance, light.PositionRange.w);
	float intensity = 1.0f;

	const uint lightClass = (uint)light.DirectionType.w;
	if (lightClass == LIGHT_CLASS_SPOT)
	{
		float theta = dot(L, normalize(-light.DirectionType.xyz));
		float innerCutoff = light.AngleParams.x;
		float outerCutoff = light.AngleParams.y;
		intensity = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));
		if (intensity <= 0.0f)
		{
			return;
		}
	}
	else if (lightClass != LIGHT_CLASS_POINT)
	{
		return;
	}
	
	float shadow = 1.0f;
	if (applyShadow)
	{
		shadow = EvaluateShadow(light.ShadowIndex, lightClass, worldPos, N, L);
	}

	float diff = max(dot(N, L), 0.0f);
	diffuseLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * diff * attenuation * intensity * shadow;

	float3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0f), 32.0f);
	float3 specularLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * spec * attenuation * intensity * shadow;

	totalLighting = diffuseLighting + specularLighting;
}

uint ComputeZSlice(float viewZ)
{
	float z = max(viewZ, NearZ);
	float sliceF = log(z) * LogZScale + LogZBias;
	return clamp((uint)floor(sliceF), 0, ClusterCountZ - 1);
}

uint ComputeClusterIndex(float4 svPosition, float3 worldPos)
{
	uint2 pixel = uint2(svPosition.xy);
	uint tileX = min(pixel.x / 16u, ClusterCountX - 1u);
	uint tileY = min(pixel.y / 16u, ClusterCountY - 1u);

	float3 viewPos = mul(float4(worldPos, 1.0f), ClusterView).xyz;
	float viewDepth = max(viewPos.x, NearZ);
	uint zSlice = ComputeZSlice(viewDepth);

	return zSlice * (ClusterCountX * ClusterCountY) + tileY * ClusterCountX + tileX;
}

float4 ComputeObjectLocalLighting(uint localLightListOffset, uint localLightListCount, float3 worldPos, float3 N, float3 V)
{
    float4 lighting = 0.0f.xxxx;

    [loop]
    for (uint i = 0; i < localLightListCount; ++i)
    {
        uint lightIndex = ObjectLightIndices[localLightListOffset + i];
        if (lightIndex < LocalLightCount)
        {
            lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
        }
    }

    return lighting;
}

float4 ComputeObjectLocalLightingLambert(uint localLightListOffset, uint localLightListCount, float3 worldPos, float3 N)
{
	float4 lighting = 0.0f.xxxx;

	[loop]
	for (uint i = 0; i < localLightListCount; ++i)
	{
		uint lightIndex = ObjectLightIndices[localLightListOffset + i];
		if (lightIndex < LocalLightCount)
		{
			FLocalLightGPU light = LocalLights[lightIndex];

			float3 toLight = light.PositionRange.xyz - worldPos;
			float distance = length(toLight);

			if (distance < light.PositionRange.w)
			{
				float3 L = toLight / max(distance, 1.0e-5f);
				float diff = max(dot(N, L), 0.0f);
				float atten = CalculateAttenuation(distance, light.PositionRange.w);
				uint lightClass = (uint)light.DirectionType.w;

				if (lightClass == LIGHT_CLASS_POINT)
				{
					lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten, 1.0f);
				}
				else if (lightClass == LIGHT_CLASS_SPOT)
				{
					float theta = dot(L, normalize(-light.DirectionType.xyz));
					float innerCutoff = light.AngleParams.x;
					float outerCutoff = light.AngleParams.y;
					float cone = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));
					lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten * cone, 1.0f);
				}
			}
		}
	}

	return lighting;
}

void ComputeObjectLocalLightingContributions(
	uint localLightListOffset,
	uint localLightListCount,
	float3 worldPos,
	float3 N,
	float3 V,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	[loop]
	for (uint i = 0; i < localLightListCount; ++i)
	{
		uint lightIndex = ObjectLightIndices[localLightListOffset + i];
		if (lightIndex < LocalLightCount)
		{
			float3 lightTotal;
			float3 lightDiffuse;
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, true, lightTotal, lightDiffuse);
			totalLighting += lightTotal;
			diffuseLighting += lightDiffuse;
		}
	}
}

FLightClusterHeader GetClusterLightHeader(float4 svPosition, float3 worldPos)
{
	uint clusterIndex = ComputeClusterIndex(svPosition, worldPos);
	return ClusterLightHeaders[clusterIndex];
}

float4 ComputeClusteredLocalLighting(float4 svPosition, float3 worldPos, float3 N, float3 V)
{
	if (LightingEnabled == 0 || LocalLightCount == 0)
		return 0.0f.xxxx;

	FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);
	float4 lighting = 0.0f.xxxx;

	[loop]
	for (uint i = 0; i < header.Count; ++i)
	{
		uint lightIndex = ClusterLightIndices[header.Offset + i];
		if (lightIndex < LocalLightCount)
		{
			lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
		}
	}

	return lighting;
}

void ComputeClusteredLocalLightingContributions(
	float4 svPosition,
	float3 worldPos,
	float3 N,
	float3 V,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	if (LightingEnabled == 0 || LocalLightCount == 0)
	{
		return;
	}

	FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);

	[loop]
	for (uint i = 0; i < header.Count; ++i)
	{
		uint lightIndex = ClusterLightIndices[header.Offset + i];
		if (lightIndex < LocalLightCount)
		{
			float3 lightTotal;
			float3 lightDiffuse;
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, true, lightTotal, lightDiffuse);
			totalLighting += lightTotal;
			diffuseLighting += lightDiffuse;
		}
	}
}

float4 ComputeClusteredLocalLightingLambert(float4 svPosition, float3 worldPos, float3 N)
{
    if (LightingEnabled == 0 || LocalLightCount == 0)
        return 0.0f.xxxx;

    float4 lighting = 0.0f.xxxx;
    FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);

    [loop]
    for (uint i = 0; i < header.Count; ++i)
    {
        uint lightIndex = ClusterLightIndices[header.Offset + i];
        if (lightIndex < LocalLightCount)
        {
            FLocalLightGPU light = LocalLights[lightIndex];

            float3 toLight = light.PositionRange.xyz - worldPos;
            float distance = length(toLight);

            if (distance < light.PositionRange.w)
            {
                float3 L = toLight / max(distance, 1.0e-5f);
                float diff = max(dot(N, L), 0.0f);
	                float atten = CalculateAttenuation(distance, light.PositionRange.w);

                uint lightClass = (uint)light.DirectionType.w;

                if (lightClass == LIGHT_CLASS_POINT)
                {
                    lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten, 1.0f);
                }
                else if (lightClass == LIGHT_CLASS_SPOT)
                {
                    float theta = dot(L, normalize(-light.DirectionType.xyz));
                    float innerCutoff = light.AngleParams.x;
                    float outerCutoff = light.AngleParams.y;
                    float cone = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));
                    float shadow = EvaluateShadow(light.ShadowIndex, LIGHT_CLASS_SPOT, worldPos, N, L);

                    lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten * cone * shadow, 1.0f);
                }
            }
        }
    }

    return lighting;
}

float3 HeatmapColor(float t)
{
    t = saturate(t);

    const float3 c0 = float3(0.0f, 0.0f, 0.0f);
    const float3 c1 = float3(0.0f, 0.0f, 1.0f);
    const float3 c2 = float3(0.0f, 1.0f, 1.0f);
    const float3 c3 = float3(0.0f, 1.0f, 0.0f);
    const float3 c4 = float3(1.0f, 1.0f, 0.0f);
    const float3 c5 = float3(1.0f, 0.0f, 0.0f);

    if (t < 0.2f) return lerp(c0, c1, t / 0.2f);
    if (t < 0.4f) return lerp(c1, c2, (t - 0.2f) / 0.2f);
    if (t < 0.6f) return lerp(c2, c3, (t - 0.4f) / 0.2f);
    if (t < 0.8f) return lerp(c3, c4, (t - 0.6f) / 0.2f);
    return lerp(c4, c5, (t - 0.8f) / 0.2f);
}

float4 VisualizeClusterLightCulling(float4 svPosition, float3 worldPos)
{
    FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);
    const float maxVisualizedLights = 16.0f;
    float normalized = saturate((float)header.RawCount / maxVisualizedLights);
    float3 color = HeatmapColor(normalized);
    return float4(color, 1.0f);
}

#endif
