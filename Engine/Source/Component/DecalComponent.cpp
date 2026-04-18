#include "Component/DecalComponent.h"

#include <algorithm>

#include "Core/Paths.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

namespace
{
	constexpr const char* GLinearColorEncoding      = "Linear";
	constexpr const char* GBaseColorTintEncodingKey = "BaseColorTintEncoding";
}

IMPLEMENT_RTTI(UDecalComponent, UPrimitiveComponent)

void UDecalComponent::BumpRevision(uint32& InOutRevision)
{
	++InOutRevision;
	if (InOutRevision == 0)
	{
		InOutRevision = 1;
	}
}

void UDecalComponent::MarkDecalVisibleDirty()
{
	BumpRevision(VisibleRevision);
}

void UDecalComponent::MarkDecalClusterDirty()
{
	BumpRevision(ClusterRevision);
}

void UDecalComponent::MarkTransformDirty()
{
	UPrimitiveComponent::MarkTransformDirty();
	MarkDecalVisibleDirty();
	MarkDecalClusterDirty();
}

void UDecalComponent::PostConstruct()
{
	bCanEverTick = true;
	bTickInEditor = true;
	bDrawDebugBounds = true;
	UpdateBounds();
	FadeIn(FadeInDuration);
}

void UDecalComponent::Tick(float DeltaTime)
{
	UPrimitiveComponent::Tick(DeltaTime);

	if (FadeState == EDecalFadeState::None)
	{
		return;
	}

	FadeElapsed += DeltaTime;
	const float T = (FadeDuration > 0.0f)
		? std::clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f)
		: 1.0f;

	if (FadeState == EDecalFadeState::FadeIn)
	{
		BaseColorTint.A = T;
		MarkDecalVisibleDirty();
		if (T >= 1.0f)
		{
			FadeState = EDecalFadeState::None;
			MarkDecalVisibleDirty();
		}
	}
	else // FadeOut
	{
		BaseColorTint.A = 1.0f - T;
		MarkDecalVisibleDirty();
		if (T >= 1.0f)
		{
			FadeState = EDecalFadeState::None;
			MarkDecalVisibleDirty();
			if (bDisableOnFadeOutComplete)
			{
				bEnabled = false;
				MarkDecalClusterDirty();
			}
		}
	}
}

void UDecalComponent::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}

	bEnabled = bInEnabled;
	MarkDecalVisibleDirty();
	MarkDecalClusterDirty();
}

void UDecalComponent::FadeIn(float Duration)
{
	bEnabled = true;
	FadeState = EDecalFadeState::FadeIn;
	FadeDuration = Duration;
	FadeElapsed = 0.0f;
	BaseColorTint.A = 0.0f;
	MarkDecalVisibleDirty();
	MarkDecalClusterDirty();
}

void UDecalComponent::FadeOut(float Duration, bool bDisableOnComplete)
{
	FadeState = EDecalFadeState::FadeOut;
	FadeDuration = Duration;
	FadeElapsed = 0.0f;
	bDisableOnFadeOutComplete = bDisableOnComplete;
	BaseColorTint.A = 1.0f;
	MarkDecalVisibleDirty();
	MarkDecalClusterDirty();
}

void UDecalComponent::SetFadeInDuration(float Duration)
{
	const float Sanitized = (std::max)(0.05f, Duration);
	if (FadeInDuration == Sanitized)
	{
		return;
	}

	FadeInDuration = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetFadeOutDuration(float Duration)
{
	const float Sanitized = (std::max)(0.05f, Duration);
	if (FadeOutDuration == Sanitized)
	{
		return;
	}

	FadeOutDuration = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetSize(const FVector2& InSize)
{
	const FVector2 Sanitized(
		(std::max)(0.0f, InSize.X),
		(std::max)(0.0f, InSize.Y));
	if (Size != Sanitized)
	{
		Size = Sanitized;
		UpdateBounds();
		MarkDecalVisibleDirty();
		MarkDecalClusterDirty();
	}
}

void UDecalComponent::SetProjectionDepth(float InProjectionDepth)
{
	const float Sanitized = (std::max)(0.0f, InProjectionDepth);
	if (ProjectionDepth != Sanitized)
	{
		ProjectionDepth = Sanitized;
		UpdateBounds();
		MarkDecalVisibleDirty();
		MarkDecalClusterDirty();
	}
}

void UDecalComponent::SetExtents(const FVector& InExtents)
{
	const FVector Sanitized(
		(std::max)(0.0f, InExtents.X),
		(std::max)(0.0f, InExtents.Y),
		(std::max)(0.0f, InExtents.Z));

	const float NewProjectionDepth = Sanitized.X * 2.0f;
	const FVector2 NewSize(Sanitized.Y * 2.0f, Sanitized.Z * 2.0f);
	if (Size != NewSize || ProjectionDepth != NewProjectionDepth)
	{
		Size = NewSize;
		ProjectionDepth = NewProjectionDepth;
		UpdateBounds();
		MarkDecalVisibleDirty();
		MarkDecalClusterDirty();
	}
}

void UDecalComponent::SetUVMin(const FVector2& InUVMin)
{
	if (UVMin == InUVMin)
	{
		return;
	}

	UVMin = InUVMin;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetUVMax(const FVector2& InUVMax)
{
	if (UVMax == InUVMax)
	{
		return;
	}

	UVMax = InUVMax;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetTexturePath(const std::wstring& InPath)
{
	if (TexturePath == InPath)
	{
		return;
	}

	TexturePath = InPath;
	TextureIndex = 0;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetRenderFlags(uint32 InRenderFlags)
{
	if (RenderFlags == InRenderFlags)
	{
		return;
	}

	RenderFlags = InRenderFlags;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetPriority(uint32 InPriority)
{
	if (Priority == InPriority)
	{
		return;
	}

	Priority = InPriority;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetReceiverLayerMask(uint32 InReceiverLayerMask)
{
	if (ReceiverLayerMask == InReceiverLayerMask)
	{
		return;
	}

	ReceiverLayerMask = InReceiverLayerMask;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetAllowAngle(float InDegrees)
{
	const float Sanitized = std::clamp(InDegrees, 0.0f, 180.0f);
	if (AllowAngle == Sanitized)
	{
		return;
	}

	AllowAngle = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetBaseColorTint(const FLinearColor& InBaseColorTint)
{
	if (BaseColorTint.R == InBaseColorTint.R
		&& BaseColorTint.G == InBaseColorTint.G
		&& BaseColorTint.B == InBaseColorTint.B
		&& BaseColorTint.A == InBaseColorTint.A)
	{
		return;
	}

	BaseColorTint = InBaseColorTint;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetNormalBlend(float InNormalBlend)
{
	const float Sanitized = std::clamp(InNormalBlend, 0.0f, 1.0f);
	if (NormalBlend == Sanitized)
	{
		return;
	}

	NormalBlend = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetRoughnessBlend(float InRoughnessBlend)
{
	const float Sanitized = std::clamp(InRoughnessBlend, 0.0f, 1.0f);
	if (RoughnessBlend == Sanitized)
	{
		return;
	}

	RoughnessBlend = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetEmissiveBlend(float InEmissiveBlend)
{
	const float Sanitized = std::clamp(InEmissiveBlend, 0.0f, 1.0f);
	if (EmissiveBlend == Sanitized)
	{
		return;
	}

	EmissiveBlend = Sanitized;
	MarkDecalVisibleDirty();
}

void UDecalComponent::SetEdgeFade(float InEdgeFade)
{
	const float Sanitized = (std::max)(0.0f, InEdgeFade);
	if (EdgeFade == Sanitized)
	{
		return;
	}

	EdgeFade = Sanitized;
	MarkDecalVisibleDirty();
}

FBoxSphereBounds UDecalComponent::GetLocalBounds() const
{
	const FVector Extents = GetExtents();
	return { FVector::ZeroVector, Extents.Size(), Extents };
}

void UDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FString TexturePathString;
	if (!TexturePath.empty())
	{
		TexturePathString = FPaths::ToRelativePath(FPaths::FromWide(TexturePath));
	}

	FVector4 BaseColorTintVector = BaseColorTint.ToVector4();
	FString BaseColorTintEncoding = GLinearColorEncoding;

	Ar.Serialize("Enabled", bEnabled);
	Ar.Serialize("Size", Size);
	Ar.Serialize("ProjectionDepth", ProjectionDepth);
	Ar.Serialize("UVMin", UVMin);
	Ar.Serialize("UVMax", UVMax);
	Ar.Serialize("TexturePath", TexturePathString);
	Ar.Serialize("RenderFlags", RenderFlags);
	Ar.Serialize("Priority", Priority);
	Ar.Serialize("ReceiverLayerMask", ReceiverLayerMask);
	Ar.Serialize("BaseColorTint", BaseColorTintVector);
	Ar.Serialize(GBaseColorTintEncodingKey, BaseColorTintEncoding);
	Ar.Serialize("NormalBlend", NormalBlend);
	Ar.Serialize("RoughnessBlend", RoughnessBlend);
	Ar.Serialize("EmissiveBlend", EmissiveBlend);
	Ar.Serialize("EdgeFade", EdgeFade);
	Ar.Serialize("AllowAngle", AllowAngle);

	if (Ar.IsLoading())
	{
		Size.X = (std::max)(0.0f, Size.X);
		Size.Y = (std::max)(0.0f, Size.Y);
		ProjectionDepth = (std::max)(0.0f, ProjectionDepth);

		if (UVMax.X < UVMin.X)
		{
			std::swap(UVMin.X, UVMax.X);
		}
		if (UVMax.Y < UVMin.Y)
		{
			std::swap(UVMin.Y, UVMax.Y);
		}

		TexturePath = TexturePathString.empty()
			? std::wstring()
			: FPaths::ToWide(FPaths::ToAbsolutePath(TexturePathString));

		TextureIndex = 0;

		const bool bStoredAsLinear = (BaseColorTintEncoding == GLinearColorEncoding);
		BaseColorTint = FLinearColor::DecodeSerializedColor(BaseColorTintVector, bStoredAsLinear);
		NormalBlend = std::clamp(NormalBlend, 0.0f, 1.0f);
		RoughnessBlend = std::clamp(RoughnessBlend, 0.0f, 1.0f);
		EmissiveBlend = std::clamp(EmissiveBlend, 0.0f, 1.0f);
		EdgeFade = (std::max)(0.0f, EdgeFade);
		VisibleRevision = 1;
		ClusterRevision = 1;

		UpdateBounds();
	}
}

void UDecalComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UDecalComponent* DuplicatedDecalComponent = static_cast<UDecalComponent*>(DuplicatedObject);
	DuplicatedDecalComponent->bEnabled = bEnabled;
	DuplicatedDecalComponent->Size = Size;
	DuplicatedDecalComponent->ProjectionDepth = ProjectionDepth;
	DuplicatedDecalComponent->UVMin = UVMin;
	DuplicatedDecalComponent->UVMax = UVMax;
	DuplicatedDecalComponent->TexturePath = TexturePath;
	DuplicatedDecalComponent->TextureIndex = 0;
	DuplicatedDecalComponent->RenderFlags = RenderFlags;
	DuplicatedDecalComponent->Priority = Priority;
	DuplicatedDecalComponent->ReceiverLayerMask = ReceiverLayerMask;
	DuplicatedDecalComponent->BaseColorTint = BaseColorTint;
	DuplicatedDecalComponent->NormalBlend = NormalBlend;
	DuplicatedDecalComponent->RoughnessBlend = RoughnessBlend;
	DuplicatedDecalComponent->EmissiveBlend = EmissiveBlend;
	DuplicatedDecalComponent->EdgeFade = EdgeFade;
	DuplicatedDecalComponent->AllowAngle = AllowAngle;
	DuplicatedDecalComponent->VisibleRevision = 1;
	DuplicatedDecalComponent->ClusterRevision = 1;
	DuplicatedDecalComponent->UpdateBounds();
}
