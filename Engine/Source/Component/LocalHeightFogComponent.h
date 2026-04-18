#pragma once

#include "Component/PrimitiveComponent.h"
#include "Math/LinearColor.h"

class FArchive;

class ENGINE_API ULocalHeightFogComponent : public UPrimitiveComponent
{
	DECLARE_RTTI(ULocalHeightFogComponent, UPrimitiveComponent)
public:
	void PostConstruct() override;
	bool IsPickable() const override { return false; }
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	FBoxSphereBounds GetLocalBounds() const override;

	float FogDensity = 0.2f;
	float FogHeightFalloff = 0.2f;
	float FogMaxOpacity = 1.0f;
	float AllowBackground = 1.0f;
	FVector FogExtents = FVector(1.0f, 1.0f, 1.0f);
	FLinearColor FogInscatteringColor = FLinearColor::FromSRGB(FVector4(0.75f, 0.80f, 0.90f, 0.5f));
};
