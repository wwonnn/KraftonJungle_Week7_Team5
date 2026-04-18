#include "Object/Class.h"
#include "Serializer/Archive.h"
#include "Renderer/Resources/Material/Material.h"
#include "Component/MeshComponent.h"

#include "Debug/EngineLog.h"
#include "Math/LinearColor.h"
#include "Renderer/Resources/Material/MaterialManager.h"

namespace
{
	constexpr const char* GMaterialBaseColorCountKey = "MaterialBaseColorCount";
	constexpr const char* GMaterialBaseColorKeyPrefix = "MaterialBaseColor_";
	constexpr const char* GMaterialBaseColorEncodingKey = "MaterialBaseColorEncoding";
	constexpr const char* GLinearColorEncoding = "Linear";

	std::shared_ptr<FMaterial> DuplicateMaterialInstance(const std::shared_ptr<FMaterial>& SourceMaterial)
	{
		if (!SourceMaterial)
		{
			return nullptr;
		}

		if (std::unique_ptr<FDynamicMaterial> DynamicMaterial = SourceMaterial->CreateDynamicMaterial())
		{
			return std::shared_ptr<FMaterial>(DynamicMaterial.release());
		}

		return SourceMaterial;
	}
}

IMPLEMENT_RTTI(UMeshComponent, UPrimitiveComponent)

void UMeshComponent::SetMaterial(int32 Index, const std::shared_ptr<FMaterial>& InMaterial)
{
	if (Index >= 0)
	{
		if (Index >= Materials.size())
		{
			Materials.resize(Index + 1, nullptr);
		}
		Materials[Index] = InMaterial;
	}
}

std::shared_ptr<FMaterial> UMeshComponent::GetMaterial(int32 Index) const
{
	if (Index >= 0 && Index < Materials.size()) return Materials[Index];
	return nullptr;
}

void UMeshComponent::DuplicateMaterialsTo(UMeshComponent* DuplicatedComponent) const
{
	DuplicatedComponent->Materials.clear();
	for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(Materials.size()); ++MaterialIndex)
	{
		DuplicatedComponent->SetMaterial(MaterialIndex, DuplicateMaterialInstance(Materials[MaterialIndex]));
	}
}

void UMeshComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);
	DuplicateMaterialsTo(static_cast<UMeshComponent*>(DuplicatedObject));
}

void UMeshComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	if (Ar.IsSaving())
	{
		FString MaterialBaseColorEncoding = GLinearColorEncoding;
		TArray<FString> MaterialNames;
		for (const std::shared_ptr<FMaterial>& Material : Materials)
		{
			if (Material) MaterialNames.push_back(Material->GetOriginName());
			else MaterialNames.push_back("");
		}
		Ar.SerializeStringArray("Materials", MaterialNames);

		int32 MaterialBaseColorCount = static_cast<int32>(Materials.size());
		Ar.Serialize(GMaterialBaseColorEncodingKey, MaterialBaseColorEncoding);
		Ar.Serialize(GMaterialBaseColorCountKey, MaterialBaseColorCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialBaseColorCount; ++MaterialIndex)
		{
			FVector4 BaseColor(1.0f, 1.0f, 1.0f, 1.0f);
			if (const std::shared_ptr<FMaterial>& Material = Materials[MaterialIndex])
			{
				BaseColor = Material->GetVectorParameter("BaseColor");
			}

			Ar.Serialize(FString(GMaterialBaseColorKeyPrefix) + std::to_string(MaterialIndex), BaseColor);
		}
	}
	else
	{
		FString MaterialBaseColorEncoding;
		if (Ar.Contains("Materials"))
		{
			TArray<FString> MaterialNames;
			Ar.SerializeStringArray("Materials", MaterialNames);

			Materials.clear();
			for (const FString& MaterialName : MaterialNames)
			{
				if (!MaterialName.empty())
				{
					std::shared_ptr<FMaterial> LoadedMaterial = FMaterialManager::Get().FindByName(MaterialName);
					Materials.push_back(LoadedMaterial);
				}
				else Materials.push_back(nullptr);
			}
		}

		int32 MaterialBaseColorCount = 0;
		Ar.Serialize(GMaterialBaseColorEncodingKey, MaterialBaseColorEncoding);
		Ar.Serialize(GMaterialBaseColorCountKey, MaterialBaseColorCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialBaseColorCount; ++MaterialIndex)
		{
			const FString BaseColorKey = FString(GMaterialBaseColorKeyPrefix) + std::to_string(MaterialIndex);
			if (!Ar.Contains(BaseColorKey) || MaterialIndex < 0 || MaterialIndex >= static_cast<int32>(Materials.size()))
			{
				continue;
			}

			FVector4 BaseColor(1.0f, 1.0f, 1.0f, 1.0f);
			Ar.Serialize(BaseColorKey, BaseColor);
			BaseColor = FLinearColor::DecodeSerializedVector(BaseColor, MaterialBaseColorEncoding == GLinearColorEncoding);

			if (!Materials[MaterialIndex])
			{
				continue;
			}

			std::shared_ptr<FMaterial> MaterialInstance = DuplicateMaterialInstance(Materials[MaterialIndex]);
			if (!MaterialInstance)
			{
				continue;
			}

			const float BaseColorArray[4] = { BaseColor.X, BaseColor.Y, BaseColor.Z, BaseColor.W };
			MaterialInstance->SetParameterData("BaseColor", BaseColorArray, sizeof(BaseColorArray));
			Materials[MaterialIndex] = MaterialInstance;
		}
	}
}

/*
void UMeshComponent::Serialize(FArchive& Ar)
{
	UUPrimitiveComponent::Serialize(Ar);

	uint32 MatCount = static_cast<uint32>(Materials.size());
	Ar.Serialize("MaterialCount", MatCount);

	if (!Ar.IsSaving())
	{
		Materials.resize(MatCount, nullptr);
	}

	for (uint32 i = 0; i < MatCount; ++i)
	{
		FString MatName;
		MatName = Materials[0]->GetOriginName();
		FString KeyName = FString("Material_") + std::to_string(i).c_str();
		Ar.Serialize(KeyName, MatName);

		if (!Ar.IsSaving() && !MatName.empty())
		{
			// TODO: 나중에 머티리얼 매니저가 생기면 주석 해제
			// Materials[i] = FMaterialManager::LoadMaterial(MatName);
		}
	}
}
*/
