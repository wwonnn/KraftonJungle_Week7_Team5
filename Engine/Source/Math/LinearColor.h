#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FLinearColor
{
	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;

	FLinearColor() = default;

	explicit FLinearColor(const float* Color) :
		R(Color[0]), G(Color[1]), B(Color[2]), A(Color[3])
	{
	}

	FLinearColor(float InR, float InG, float InB, float InA) :
		R(InR), G(InG), B(InB), A(InA)
	{
	}

	FVector4 ToVector4() const
	{
		return FVector4(R, G, B, A);
	}

	FVector4 ToSRGBVector4() const;

	static float SRGBChannelToLinear(float Value);
	static float LinearChannelToSRGB(float Value);

	static bool IsUnitIntervalRGB(const FVector4& Value);

	static FLinearColor FromSRGB(const float* Color);
	static FLinearColor FromSRGB(const FVector4& Color);
	static FVector4 SRGBToLinear(const FVector4& Color);
	static FVector4 LinearToSRGB(const FVector4& Color);
	static FLinearColor DecodeSerializedColor(const FVector4& StoredColor, bool bStoredAsLinear);
	static FVector4 DecodeSerializedVector(const FVector4& StoredColor, bool bStoredAsLinear);

	static const FLinearColor White;
	static const FLinearColor Black;
};
