#include "DirectionalLightComponent.h"

#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UDirectionalLightComponent, ULightComponent)

void UDirectionalLightComponent::PostConstruct()
{
	ULightComponent::PostConstruct();
	IntensityUnits = ELightUnits::Lux;
	
	Intensity = 2.0f;
	ShadowResolutionScale = 8.0f;

	CascadeCount = 4;
	ShadowFarZ = 200.0f;
	SplitLambda = 0.7f;
	CascadeTransitionValue = 0.1f;
}

void UDirectionalLightComponent::SetCascadeCount(int32 InCascadeCount)
{
	if (CascadeCount != InCascadeCount)
	{
		CascadeCount = InCascadeCount;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetShadowFarZ(float InShadowFarZ)
{
	if (ShadowFarZ != InShadowFarZ)
	{
		ShadowFarZ = InShadowFarZ;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetSplitLambda(float InSplitLambda)
{
	if (SplitLambda != InSplitLambda)
	{
		SplitLambda = InSplitLambda;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeTransitionValue(float InCascadeTransitionValue)
{
	if (CascadeTransitionValue != InCascadeTransitionValue)
	{
		CascadeTransitionValue = InCascadeTransitionValue;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeBias(int32 Index, float InBias)
{
	if (Index >= 0 && Index < 4 && CascadeBiases[Index] != InBias)
	{
		CascadeBiases[Index] = InBias;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeSlopeBias(int32 Index, float InBias)
{
	if (Index >= 0 && Index < 4 && CascadeSlopeBiases[Index] != InBias)
	{
		CascadeSlopeBiases[Index] = InBias;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
	ULightComponent::Serialize(Ar);

	Ar.Serialize("CascadeCount", CascadeCount);
	Ar.Serialize("ShadowFarZ", ShadowFarZ);
	Ar.Serialize("SplitLambda", SplitLambda);
	Ar.Serialize("CascadeTransitionValue", CascadeTransitionValue);

	for (int32 i = 0; i < 4; ++i)
	{
		Ar.Serialize(std::string("CascadeBias") + std::to_string(i), CascadeBiases[i]);
		Ar.Serialize(std::string("CascadeSlopeBias") + std::to_string(i), CascadeSlopeBiases[i]);
	}

	if (Ar.IsLoading())
	{
		CascadeCount = (std::max)(1, CascadeCount);
	}
}

void UDirectionalLightComponent::MarkTransformDirty()
{
	ULightComponent::MarkTransformDirty();
}

bool UDirectionalLightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	return UnitType == ELightUnits::Lux;
}

float UDirectionalLightComponent::ComputePhotometricScale() const
{
	return 1.0f;
}
