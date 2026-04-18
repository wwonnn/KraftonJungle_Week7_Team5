#include "Component/FireBallComponent.h"

#include "Object/Class.h"
#include "Serializer/Archive.h"

namespace
{
	constexpr const char* GLinearColorEncoding  = "Linear";
	constexpr const char* GFireColorEncodingKey = "ColorEncoding";
}

IMPLEMENT_RTTI(UFireBallComponent, UPrimitiveComponent)

void UFireBallComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FVector4 FireColor = Color.ToVector4();
	FString FireColorEncoding = GLinearColorEncoding;

	Ar.Serialize("Color", FireColor);
	Ar.Serialize(GFireColorEncodingKey, FireColorEncoding);
	Ar.Serialize("Intensity", Intensity);
	Ar.Serialize("Radius", Radius);
	Ar.Serialize("RadiusFallOff", RadiusFallOff);

	if (Ar.IsLoading())
	{
		Intensity = (std::max)(0.0f, Intensity);
		Radius = (std::max)(0.0f, Radius);
		RadiusFallOff = (std::max)(0.0f, RadiusFallOff);
		const bool bStoredAsLinear = (FireColorEncoding == GLinearColorEncoding);
		Color = FLinearColor::DecodeSerializedColor(FireColor, bStoredAsLinear);
	}
}

void UFireBallComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UFireBallComponent* DuplicatedFogComponent = static_cast<UFireBallComponent*>(DuplicatedObject);
	DuplicatedFogComponent->Intensity = Intensity;
	DuplicatedFogComponent->Radius = Radius;
	DuplicatedFogComponent->RadiusFallOff = RadiusFallOff;
	DuplicatedFogComponent->Color = Color;
}
