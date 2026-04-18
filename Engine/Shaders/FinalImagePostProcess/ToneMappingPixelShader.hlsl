Texture2D<float4> SceneTexture : register(t0);
SamplerState SceneSampler : register(s0);

cbuffer ToneMappingConstants : register(b0)
{
	float Exposure;
	float ShoulderStrength;
	float LinearWhite;
	float Pad0;
};

struct PSInput
{
	float4 Position : SV_Position;
	float2 UV       : TEXCOORD0;
};

float3 ACESFilm(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ReinhardExtended(float3 color, float whitePoint)
{
	float white2 = whitePoint * whitePoint;
	return (color * (1.0f + color / white2)) / (1.0f + color);
}

float4 main(PSInput Input) : SV_Target
{
	float3 hdrColor = SceneTexture.Sample(SceneSampler, Input.UV).rgb;

	hdrColor *= Exposure;

	float3 ldrColor = ACESFilm(hdrColor);

	// float3 ldrColor = ReinhardExtended(hdrColor, LinearWhite);

	return float4(ldrColor, 1.0f);
}