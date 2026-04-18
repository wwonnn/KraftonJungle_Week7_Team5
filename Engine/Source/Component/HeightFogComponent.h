#pragma once

#include "Component/PrimitiveComponent.h"

#include "Math/LinearColor.h"

class FArchive;

class ENGINE_API UHeightFogComponent : public UPrimitiveComponent
{
	DECLARE_RTTI(UHeightFogComponent, UPrimitiveComponent)
public:
	void PostConstruct() override;
	bool IsPickable() const override { return false; }
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

	float FogDensity = 0.2f;
	float FogHeightFalloff = 0.2f;
	float StartDistance = 0.0f;
	float FogCutoffDistance = 0.0f;
	float FogMaxOpacity = 1.0f;
	float AllowBackground = 1.0f;


	FLinearColor FogInscatteringColor = FLinearColor::FromSRGB(FVector4(0.75f, 0.80f, 0.90f, 0.5f));
};
