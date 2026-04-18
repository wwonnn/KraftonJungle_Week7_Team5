#include "Component/HeightFogComponent.h"

#include <algorithm>

#include "Object/Class.h"
#include "Serializer/Archive.h"

namespace
{
	constexpr const char* GLinearColorEncoding = "Linear";
	constexpr const char* GFogColorEncodingKey = "FogInscatteringColorEncoding";
}

IMPLEMENT_RTTI(UHeightFogComponent, UPrimitiveComponent)

void UHeightFogComponent::PostConstruct()
{
	bDrawDebugBounds = false;
}

void UHeightFogComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FVector4 FogColor = FogInscatteringColor.ToVector4();
	FString FogColorEncoding = GLinearColorEncoding;

	Ar.Serialize("FogDensity", FogDensity);
	Ar.Serialize("FogHeightFalloff", FogHeightFalloff);
	Ar.Serialize("StartDistance", StartDistance);
	Ar.Serialize("FogCutoffDistance", FogCutoffDistance);
	Ar.Serialize("FogMaxOpacity", FogMaxOpacity);
	Ar.Serialize("FogInscatteringColor", FogColor);
	Ar.Serialize(GFogColorEncodingKey, FogColorEncoding);
	Ar.Serialize("FogAllowBackground", AllowBackground);

	if (Ar.IsLoading())
	{
		FogDensity = (std::max)(0.0f, FogDensity);
		FogHeightFalloff = (std::max)(0.0f, FogHeightFalloff);
		StartDistance = (std::max)(0.0f, StartDistance);
		FogCutoffDistance = (std::max)(0.0f, FogCutoffDistance);
		FogMaxOpacity = std::clamp(FogMaxOpacity, 0.0f, 1.0f);
		const bool bStoredAsLinear = (FogColorEncoding == GLinearColorEncoding);
		FogInscatteringColor = FLinearColor::DecodeSerializedColor(FogColor, bStoredAsLinear);
	}
}

void UHeightFogComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UHeightFogComponent* DuplicatedFogComponent = static_cast<UHeightFogComponent*>(DuplicatedObject);
	DuplicatedFogComponent->FogDensity = FogDensity;
	DuplicatedFogComponent->FogHeightFalloff = FogHeightFalloff;
	DuplicatedFogComponent->StartDistance = StartDistance;
	DuplicatedFogComponent->FogCutoffDistance = FogCutoffDistance;
	DuplicatedFogComponent->FogMaxOpacity = FogMaxOpacity;
	DuplicatedFogComponent->FogInscatteringColor = FogInscatteringColor;
	DuplicatedFogComponent->AllowBackground = AllowBackground;
}
