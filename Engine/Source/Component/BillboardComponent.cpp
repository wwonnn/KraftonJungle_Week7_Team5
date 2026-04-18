#include "Component/BillboardComponent.h"
#include "Core/Paths.h"
#include "Component/DecalComponent.h"
#include "Math/LinearColor.h"
#include "Math/Matrix.h"
#include "Object/Class.h"
#include "Renderer/Mesh/MeshData.h"
#include "Serializer/Archive.h"
#include <cmath>

namespace
{
	constexpr const char* GLinearColorEncoding       = "Linear";
	constexpr const char* GBillboardColorEncodingKey = "BaseColorEncoding";
}

IMPLEMENT_RTTI(UBillboardComponent, UPrimitiveComponent)

void UBillboardComponent::PostConstruct()
{
	bDrawDebugBounds = false;
	BillboardMesh = std::make_shared<FDynamicMesh>();
	MarkBillboardMeshDirty();
}

void UBillboardComponent::MarkBillboardMeshDirty()
{
	bBillboardMeshDirty = true;
	if (BillboardMesh)
	{
		BillboardMesh->bIsDirty = true;
	}
}

void UBillboardComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UBillboardComponent* DuplicatedBillboardComponent = static_cast<UBillboardComponent*>(DuplicatedObject);
	DuplicatedBillboardComponent->TexturePath = TexturePath;
	DuplicatedBillboardComponent->Size = Size;
	DuplicatedBillboardComponent->UVMin = UVMin;
	DuplicatedBillboardComponent->UVMax = UVMax;
	DuplicatedBillboardComponent->BaseColor = BaseColor;
	DuplicatedBillboardComponent->AxisLockMode = AxisLockMode;
	DuplicatedBillboardComponent->MarkBillboardMeshDirty();
}

void UBillboardComponent::PostDuplicate(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	UPrimitiveComponent::PostDuplicate(DuplicatedObject, Context);

	UBillboardComponent* DuplicatedBillboardComponent = static_cast<UBillboardComponent*>(DuplicatedObject);
	DuplicatedBillboardComponent->MarkBillboardMeshDirty();
	DuplicatedBillboardComponent->UpdateBounds();
}

void UBillboardComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FString TexturePathString;
	if (!TexturePath.empty())
	{
		TexturePathString = FPaths::ToRelativePath(FPaths::FromWide(TexturePath));
	}

	if (Ar.IsSaving())
	{
		FString BaseColorEncoding = GLinearColorEncoding;
		Ar.Serialize("TexturePath", TexturePathString);
		Ar.Serialize("Size", Size);
		Ar.Serialize("UVMin", UVMin);
		Ar.Serialize("UVMax", UVMax);
		Ar.Serialize("BaseColor", BaseColor);
		Ar.Serialize(GBillboardColorEncodingKey, BaseColorEncoding);
		int32 AxisLockModeValue = static_cast<int32>(AxisLockMode);
		Ar.Serialize("AxisLockMode", AxisLockModeValue);
	}
	else
	{
		FString BaseColorEncoding;
		Ar.Serialize("TexturePath", TexturePathString);
		Ar.Serialize("Size", Size);
		Ar.Serialize("UVMin", UVMin);
		Ar.Serialize("UVMax", UVMax);
		Ar.Serialize("BaseColor", BaseColor);
		Ar.Serialize(GBillboardColorEncodingKey, BaseColorEncoding);
		int32 AxisLockModeValue = static_cast<int32>(AxisLockMode);
		Ar.Serialize("AxisLockMode", AxisLockModeValue);
		if (AxisLockModeValue < static_cast<int32>(EAxisLockMode::None) || AxisLockModeValue > static_cast<int32>(EAxisLockMode::LocalZ))
		{
			AxisLockModeValue = static_cast<int32>(EAxisLockMode::None);
		}
		AxisLockMode = static_cast<EAxisLockMode>(AxisLockModeValue);

		TexturePath = TexturePathString.empty()
			? std::wstring()
			: FPaths::ToWide(FPaths::ToAbsolutePath(TexturePathString));
		BaseColor = FLinearColor::DecodeSerializedVector(BaseColor, BaseColorEncoding == GLinearColorEncoding);

		MarkBillboardMeshDirty();
		UpdateBounds();
	}
}

void UBillboardComponent::SetTexturePath(const std::wstring& InPath)
{
	if (InPath.empty())
	{
		TexturePath.clear();
		return;
	}

	const FString AbsolutePath = FPaths::ToAbsolutePath(FPaths::FromWide(InPath));
	TexturePath = std::filesystem::path(FPaths::ToWide(AbsolutePath)).lexically_normal().wstring();
}

FBoxSphereBounds UBillboardComponent::GetWorldBounds() const
{
	const FVector Center = GetWorldLocation();
	const FVector WorldScale = GetRenderWorldScale();

	const float HalfW = Size.X * 0.5f * WorldScale.X;
	const float HalfH = Size.Y * 0.5f * WorldScale.Y;
	const float HalfZ = ((HalfW > HalfH) ? HalfW : HalfH);

	const FVector BoxExtent(HalfW, HalfH, HalfZ);
	return { Center, BoxExtent.Size(), BoxExtent };
}

FRenderMesh* UBillboardComponent::GetRenderMesh() const
{
	return BillboardMesh.get();
}

void UBillboardComponent::SetBaseColor(const FVector4& InBaseColor)
{
	BaseColor = InBaseColor;
}

const FVector4& UBillboardComponent::GetBaseColor() const
{
	return BaseColor;
}

FVector UBillboardComponent::GetBillboardAxisLockVector() const
{
	switch (AxisLockMode)
	{
	case EAxisLockMode::LocalX:
		return GetWorldTransform().GetUnitAxis(EAxis::X);
	case EAxisLockMode::LocalY:
		return GetWorldTransform().GetUnitAxis(EAxis::Y);
	case EAxisLockMode::LocalZ:
		return GetWorldTransform().GetUnitAxis(EAxis::Z);
	default:
		return FVector::ZeroVector;
	}
}

FVector UBillboardComponent::GetRenderWorldScale() const
{
	return UPrimitiveComponent::GetRenderWorldScale();
}
