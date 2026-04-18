#include "DebugDrawManager.h"

#include "Actor/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Core/ShowFlags.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Math/LinearColor.h"
#include "Object/Class.h"
#include "World/World.h"

FWorldDebugDrawBucket* FDebugDrawManager::FindOrAddBucket(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	return &WorldBuckets[World];
}

const FWorldDebugDrawBucket* FDebugDrawManager::FindBucket(UWorld* World) const
{
	if (!World)
	{
		return nullptr;
	}

	const auto Found = WorldBuckets.find(World);
	return (Found != WorldBuckets.end()) ? &Found->second : nullptr;
}

void FDebugDrawManager::DrawLine(UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color)
{
	if (FWorldDebugDrawBucket* Bucket = FindOrAddBucket(World))
	{
		Bucket->Lines.push_back({ Start, End, Color });
	}
}

void FDebugDrawManager::DrawCube(UWorld* World, const FVector& Center, const FVector& Extent, const FVector4& Color)
{
	if (FWorldDebugDrawBucket* Bucket = FindOrAddBucket(World))
	{
		Bucket->Cubes.push_back({ Center, Extent, Color });
	}
}

void FDebugDrawManager::BuildPrimitiveList(
	const FShowFlags& ShowFlags,
	UWorld* World,
	FDebugPrimitiveList& OutPrimitives) const
{
	OutPrimitives.Clear();
	if (!ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
	{
		return;
	}

	if (ShowFlags.HasFlag(EEngineShowFlags::SF_Collision) && World)
	{
		DrawAllCollisionBounds(ShowFlags, World, OutPrimitives);
	}

	if (const FWorldDebugDrawBucket* Bucket = FindBucket(World))
	{
		OutPrimitives.Cubes.insert(OutPrimitives.Cubes.end(), Bucket->Cubes.begin(), Bucket->Cubes.end());
		OutPrimitives.Lines.insert(OutPrimitives.Lines.end(), Bucket->Lines.begin(), Bucket->Lines.end());
	}
}

void FDebugDrawManager::ReleaseWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}

	WorldBuckets.erase(World);
}

void FDebugDrawManager::Clear()
{
	WorldBuckets.clear();
}

void FDebugDrawManager::DrawAllCollisionBounds(const FShowFlags& ShowFlags, UWorld* World, FDebugPrimitiveList& OutPrimitives) const
{
	TArray<AActor*> AllActors = World->GetAllActors();
	for (AActor* Actor : AllActors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
			{
				continue;
			}

			UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
			if (!PrimitiveComponent->ShouldDrawDebugBounds())
			{
				continue;
			}

			if (IsArrowVisualizationPrimitive(PrimitiveComponent) || IsHiddenByArrowVisualizationShowFlags(PrimitiveComponent, ShowFlags))
			{
				continue;
			}

			const FBoxSphereBounds Bounds = PrimitiveComponent->GetWorldBounds();
			if (Bounds.BoxExtent.SizeSquared() > 0.0f)
			{
				const FVector4 Color = FLinearColor::FromSRGB(FVector4(1.0f, 0.2f, 1.0f, 1.0f)).ToVector4(); // Magenta: World Bounds
				OutPrimitives.Cubes.push_back({ Bounds.Center, Bounds.BoxExtent, Color });
			}
		}
	}
}
