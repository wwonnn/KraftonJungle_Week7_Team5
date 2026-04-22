#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"
#include "../LightCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

#if HAS_NORMAL_MAP
Texture2D NormalMap : register(t1);
#endif
Texture2D EmissiveMap : register(t2);

#include "../LightCommon.hlsli"

/*cbuffer MaterialData : register(b2)
{
	float4 EmissiveColor;
	float Shininess;
	float3 MaterialPadding;
};
*/

// Diffuse 텍스처 색상
#define TextureColor (Texture.Sample(Sampler, Input.UV) * ColorTint)
#define EmissiveTerm (EmissiveColor.rgb + EmissiveMap.Sample(Sampler, Input.UV).rgb)

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	if (VisualizationMode == LIGHT_VISUALIZATION_CLUSTER_HEATMAP)
	{
		return VisualizeClusterLightCulling(Input.Position, Input.WorldPosition);
	}

	    // ── 법선 결정 ──
#if HAS_NORMAL_MAP
    float3 N = GetNormalFromMap(NormalMap, Sampler, Input.Normal, Input.Tangent, Input.Bitangent, Input.UV);
#else
	float3 N = normalize(Input.Normal);
#endif

#if VIEWMODE_WORLD_NORMAL
	return float4(N * 0.5f + 0.5f, 1.0f);
#endif

	float3 V = normalize(CameraPosition.xyz - Input.WorldPosition);

	float4 baseColor = TextureColor;
	
#if LIGHTING_MODEL_GOURAUD

	float3 finalColor = baseColor.rgb * Input.VertexLighting.rgb + Input.VertexSpecular;
	finalColor += EmissiveTerm;
	return float4(finalColor, baseColor.a);
	
#elif LIGHTING_MODEL_LAMBERT
	
	float3 lighting = 0.0f.xxx;
    
    if (AmbientEnabled != 0)
    {
        lighting += CalculateAmbientLight(Ambient);
    }
    
    if (DirectionalLightCount > 0)
    {
		float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
		float diff = max(0.0f, dot(N, L_dir));
		lighting += Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * diff;
    }
    
	lighting += ComputeClusteredLocalLightingLambert(Input.Position, Input.WorldPosition, N).rgb;

	baseColor.rgb *= lighting;
	baseColor.rgb += EmissiveTerm;
	return float4(baseColor.rgb, baseColor.a);
	
#elif LIGHTING_MODEL_PHONG

	float3 totalLighting = 0.0f.xxx;
	float3 diffuseLighting = 0.0f.xxx;
	float3 localTotalLighting = 0.0f.xxx;
	float3 localDiffuseLighting = 0.0f.xxx;
	
    // Diffuse + Specular (Blinn-Phong)
	if (AmbientEnabled != 0)
	{
		float3 ambient = CalculateAmbientLight(Ambient).rgb;
		totalLighting += ambient;
		diffuseLighting += ambient;
	}

	if (DirectionalLightCount > 0)
	{
		float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
		float diff = max(0.0f, dot(N, L_dir));
		float3 dirDiffuse = Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * diff;
		diffuseLighting += dirDiffuse;
		totalLighting += CalculateDirectionalLight(Directional, Input.WorldPosition, N, V).rgb;
	}
	
	ComputeClusteredLocalLightingContributions(
		Input.Position,
		Input.WorldPosition,
		N,
		V,
		localTotalLighting,
		localDiffuseLighting);
	totalLighting += localTotalLighting;
	diffuseLighting += localDiffuseLighting;

	float3 specularLighting = max(totalLighting - diffuseLighting, 0.0f.xxx);
	float3 finalColor = baseColor.rgb * diffuseLighting + specularLighting;
	finalColor += EmissiveTerm;
	return float4(finalColor, baseColor.a);
	
#elif LIGHTING_MODEL_TOON

	const float ToonBandCount = 3.0f;
	const float ToonSpecThreshold = 0.99f;
	const float ToonRimThreshold = 0.70f;
	const float ToonRimStrength = 0.2f;
	const float ToonAmbientScale = 0.7f;

	float3 diffuseLighting = 0.0f.xxx;
	float3 specularLighting = 0.0f.xxx;
	float3 localDiffuseLighting = 0.0f.xxx;
	float3 localSpecularLighting = 0.0f.xxx;

	if (AmbientEnabled != 0)
	{
		diffuseLighting += CalculateAmbientLight(Ambient).rgb * ToonAmbientScale;
	}

	if (DirectionalLightCount > 0)
	{
		float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
		float3 H_dir = normalize(L_dir + V);

		float diff = max(0.0f, dot(N, L_dir));
		float toonDiff = QuantizeToonDiffuse(diff, ToonBandCount);
		diffuseLighting += Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * toonDiff;

		float spec = pow(max(0.0f, dot(N, H_dir)), max(Shininess, 1.0f));
		float toonSpec = QuantizeToonSpecular(spec, ToonSpecThreshold);
		specularLighting += Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * toonSpec;
	}

	ComputeClusteredLocalLightingToonContributions(
		Input.Position,
		Input.WorldPosition,
		N,
		V,
		ToonBandCount,
		ToonSpecThreshold,
		localDiffuseLighting,
		localSpecularLighting);

	diffuseLighting += localDiffuseLighting;
	specularLighting += localSpecularLighting;

	float rim = 1.0f - saturate(dot(N, V));
	rim = step(ToonRimThreshold, rim) * ToonRimStrength;
	float3 rimLighting = baseColor.rgb * rim;

	float3 finalColor = baseColor.rgb * diffuseLighting + specularLighting + rimLighting;
	finalColor += EmissiveTerm;
	return float4(finalColor, baseColor.a);
	
#endif

	return float4(baseColor.rgb + EmissiveTerm, baseColor.a);
}
