#include "Math/LinearColor.h"

#include <algorithm>
#include <cmath>

namespace
{
	float ClampColorChannel(float Value)
	{
		return (std::max)(0.0f, Value);
	}
}

const FLinearColor FLinearColor::White = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Black = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

FVector4 FLinearColor::ToSRGBVector4() const
{
	return LinearToSRGB(ToVector4());
}

float FLinearColor::SRGBChannelToLinear(float Value)
{
	if (Value <= 0.04045f)
	{
		return Value / 12.92f;
	}

	return std::pow((Value + 0.055f) / 1.055f, 2.4f);
}

float FLinearColor::LinearChannelToSRGB(float Value)
{
	Value = ClampColorChannel(Value);
	if (Value <= 0.0031308f)
	{
		return Value * 12.92f;
	}

	return 1.055f * std::pow(Value, 1.0f / 2.4f) - 0.055f;
}

bool FLinearColor::IsUnitIntervalRGB(const FVector4& Value)
{
	return Value.X >= 0.0f && Value.X <= 1.0f
		&& Value.Y >= 0.0f && Value.Y <= 1.0f
		&& Value.Z >= 0.0f && Value.Z <= 1.0f;
}

FLinearColor FLinearColor::FromSRGB(const float* Color)
{
	return Color
		? FromSRGB(FVector4(Color[0], Color[1], Color[2], Color[3]))
		: FLinearColor();
}

FLinearColor FLinearColor::FromSRGB(const FVector4& Color)
{
	return FLinearColor(
		SRGBChannelToLinear(Color.X),
		SRGBChannelToLinear(Color.Y),
		SRGBChannelToLinear(Color.Z),
		Color.W);
}

FVector4 FLinearColor::SRGBToLinear(const FVector4& Color)
{
	return FromSRGB(Color).ToVector4();
}

FVector4 FLinearColor::LinearToSRGB(const FVector4& Color)
{
	return FVector4(
		LinearChannelToSRGB(Color.X),
		LinearChannelToSRGB(Color.Y),
		LinearChannelToSRGB(Color.Z),
		Color.W);
}

FLinearColor FLinearColor::DecodeSerializedColor(const FVector4& StoredColor, bool bStoredAsLinear)
{
	const FVector4 LinearColor = DecodeSerializedVector(StoredColor, bStoredAsLinear);
	return FLinearColor(LinearColor.X, LinearColor.Y, LinearColor.Z, LinearColor.W);
}

FVector4 FLinearColor::DecodeSerializedVector(const FVector4& StoredColor, bool bStoredAsLinear)
{
	if (bStoredAsLinear || !IsUnitIntervalRGB(StoredColor))
	{
		return StoredColor;
	}

	return SRGBToLinear(StoredColor);
}
