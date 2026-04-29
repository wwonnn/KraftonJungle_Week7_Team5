#include "PropertyWindow.h"
#include "EditorEngine.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Actor/SpotLightFakeActor.h"
#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/RandomColorComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/LineBatchComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/LocalHeightFogComponent.h"
#include "Component/MoveComponent.h"
#include "Component/DecalComponent.h"
#include "Component/MeshDecalComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/LightComponent.h"
#include "Renderer/Features/Shadow/ShadowTypes.h"
#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/AmbientLightComponent.h"
#include "Component/MovementComponent.h"
#include "Component/RotatingMovementComponent.h"
#include "Component/ProjectileMovementComponent.h"
#include "Component/SpringArmComponent.h"
#include "Asset/ObjManager.h"
#include "Level/Level.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Object/ObjectIterator.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Core/Paths.h"
#include "Core/ShowFlags.h"
#include "Level/ScenePacketBuilder.h"
#include "Math/MathUtility.h"
#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Viewport/Viewport.h"
#include "World/World.h"

#include <algorithm>
#include <chrono>

#include "imgui_internal.h"
#include "Renderer/Features/Billboard/BillboardRenderer.h"
#include "Renderer/Features/Shadow/ShadowRenderFeature.h"

namespace
{
	using FComponentClassGetter = UClass * (*)();

	bool EditLinearColor4(const char* Label, FLinearColor& InOutColor)
	{
		const FVector4 DisplayColor  = InOutColor.ToSRGBVector4();
		float          ColorArray[4] = { DisplayColor.X, DisplayColor.Y, DisplayColor.Z, DisplayColor.W };
		if (!ImGui::ColorEdit4(Label, ColorArray))
		{
			return false;
		}

		InOutColor = FLinearColor::FromSRGB(ColorArray[0], ColorArray[1], ColorArray[2], ColorArray[3]);
		return true;
	}

	bool EditLinearColor3(const char* Label, FLinearColor& InOutColor, ImGuiColorEditFlags Flags = 0)
	{
		const FVector4 DisplayColor  = InOutColor.ToSRGBVector4();
		float          ColorArray[3] = { DisplayColor.X, DisplayColor.Y, DisplayColor.Z };
		if (!ImGui::ColorEdit3(Label, ColorArray, Flags))
		{
			return false;
		}

		InOutColor = FLinearColor::FromSRGB(ColorArray[0], ColorArray[1], ColorArray[2], InOutColor.A);
		return true;
	}

	struct FComponentAddOption
	{
		const char*           Label;
		const char*           BaseName;
		FComponentClassGetter GetClass;
	};

	const FComponentAddOption GComponentAddOptions[] =
	{
		{ "Scene Component", "SceneComponent", &USceneComponent::StaticClass },
		{ "Static Mesh Component", "StaticMeshComponent", &UStaticMeshComponent::StaticClass },
		{ "Text Component", "TextComponent", &UTextRenderComponent::StaticClass },
		{ "SubUV Component", "SubUVComponent", &USubUVComponent::StaticClass },
		{ "BillboardComponent", "BillboardComponent", &UBillboardComponent::StaticClass },
		{ "Move Component", "MoveComponent", &UMoveComponent::StaticClass },
		{ "Rotating Movement Component", "RotatingMovementComponent", &URotatingMovementComponent::StaticClass },
		{ "Projectile Movement Component", "ProjectileMovementComponent", &UProjectileMovementComponent::StaticClass },
		{ "FireBall Component", "FireBallComponent", &UFireBallComponent::StaticClass },
		{ "Decal Component", "DecalComponent", &UDecalComponent::StaticClass },
		{ "Mesh Decal Component", "MeshDecalComponent", &UMeshDecalComponent::StaticClass },
		{ "Local Height Fog Component", "LocalHeightFogComponent", &ULocalHeightFogComponent::StaticClass },
		{ "Directional Light Component", "DirectionalLightComponent", &UDirectionalLightComponent::StaticClass },
		{ "Point Light Component", "PointLightComponent", &UPointLightComponent::StaticClass },
		{ "Spot Light Component", "SpotLightComponent", &USpotLightComponent::StaticClass },
		{ "Ambient Light Component", "AmbientLightComponent", &UAmbientLightComponent::StaticClass },
	};

	constexpr auto GMaterialPreviewContextName          = "MaterialInspectorPreview";
	constexpr auto GMaterialPreviewAmbientActorName     = "MaterialPreviewAmbient";
	constexpr auto GMaterialPreviewDirectionalActorName = "MaterialPreviewDirectional";

	bool IsHiddenEditorOnlyComponent(const UActorComponent* Component)
	{
		return Component && Component->IsA(UUUIDBillboardComponent::StaticClass());
	}

	FString BuildUniqueComponentName(AActor* SelectedActor, const FString& BaseName)
	{
		if (!SelectedActor)
		{
			return BaseName;
		}

		auto HasSameName = [SelectedActor](const FString& CandidateName)
		{
			for (UActorComponent* Component : SelectedActor->GetComponents())
			{
				if (Component && !IsHiddenEditorOnlyComponent(Component) && Component->GetName() == CandidateName)
				{
					return true;
				}
			}
			return false;
		};

		FString UniqueName = BaseName;
		int32   Suffix     = 1;
		while (HasSameName(UniqueName))
		{
			UniqueName = BaseName + std::to_string(Suffix++);
		}

		return UniqueName;
	}

	void RefreshSceneComponentHierarchy(USceneComponent* Component)
	{
		if (!Component)
		{
			return;
		}

		if (Component->IsA(UPrimitiveComponent::StaticClass()))
		{
			static_cast<UPrimitiveComponent*>(Component)->UpdateBounds();
		}

		for (USceneComponent* Child : Component->GetAttachChildren())
		{
			if (IsHiddenEditorOnlyComponent(Child))
			{
				continue;
			}

			RefreshSceneComponentHierarchy(Child);
		}
	}

	bool IsSupportedTextureFile(const std::filesystem::path& Path)
	{
		std::wstring Ext = Path.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), towlower);
		return Ext == L".png" || Ext == L".dds" || Ext == L".jpg" ||
				Ext == L".jpeg" || Ext == L".tga" || Ext == L".bmp";
	}

	// FPaths::TextureDir()를 스캔해서 사용 가능한 텍스처 파일 목록을 반환.
	// 콤보 오픈 시에만 호출되므로 매번 디스크 스캔해도 비용이 작음 — 런타임에 추가된 파일도 즉시 반영.
	const TArray<std::filesystem::path>& GetAvailableTexturePaths()
	{
		using FClock = std::chrono::steady_clock;

		static TArray<std::filesystem::path> CachedPaths;
		static FClock::time_point            LastRefreshTime = FClock::time_point::min();
		const FClock::time_point             Now             = FClock::now();
		if (LastRefreshTime != FClock::time_point::min()
			&& std::chrono::duration_cast<std::chrono::milliseconds>(Now - LastRefreshTime).count() < 1000)
		{
			return CachedPaths;
		}

		TArray<std::filesystem::path> Paths;

		std::error_code             ErrorCode;
		const std::filesystem::path TextureDir = FPaths::TextureDir();
		if (!std::filesystem::exists(TextureDir, ErrorCode))
		{
			CachedPaths.clear();
			LastRefreshTime = Now;
			return CachedPaths;
		}

		for (auto& Entry : std::filesystem::recursive_directory_iterator(TextureDir, ErrorCode))
		{
			if (Entry.is_regular_file(ErrorCode) && IsSupportedTextureFile(Entry.path()))
			{
				Paths.push_back(Entry.path());
			}
		}
		std::sort(Paths.begin(), Paths.end());

		CachedPaths     = std::move(Paths);
		LastRefreshTime = Now;
		return CachedPaths;
	}

	FString GetTextureMaterialName(const std::filesystem::path& TexturePath)
	{
		std::error_code             ErrorCode;
		const std::filesystem::path RelativePath = std::filesystem::relative(TexturePath, FPaths::ProjectRoot(), ErrorCode);
		FString                     MaterialName = FPaths::FromPath(ErrorCode ? TexturePath.lexically_normal() : RelativePath.lexically_normal());
		std::replace(MaterialName.begin(), MaterialName.end(), '\\', '/');
		return MaterialName;
	}

	// Material의 현재 Normal Texture 슬롯에 대한 콤보 박스 UI.
	// None 선택 시 해제, 파일 선택 시 디스크에서 로드. 반환값은 변경 여부 (현재는 사용하지 않음).
	std::shared_ptr<FMaterial> LoadTextureMaterial(const std::filesystem::path& TexturePath)
	{
		return FMaterialManager::Get().LoadFromTexturePath(GetTextureMaterialName(TexturePath));
	}

	TMap<FString, bool> BuildTextureBackedDynamicMaterialLookup(const TArray<std::filesystem::path>& TexturePaths)
	{
		TMap<FString, bool> Lookup;
		for (const std::filesystem::path& TexturePath : TexturePaths)
		{
			const FString                    MaterialName = GetTextureMaterialName(TexturePath);
			const std::shared_ptr<FMaterial> Material     = FMaterialManager::Get().FindByName(MaterialName);
			if (!Material)
			{
				continue;
			}

			const std::shared_ptr<FMaterialTexture> MaterialTexture = Material->GetMaterialTexture();
			if (MaterialTexture && MaterialTexture->SourcePath == MaterialName)
			{
				Lookup[MaterialName] = true;
			}
		}
		return Lookup;
	}

	TArray<FString> BuildMaterialPickerNames(const TArray<FString>& MaterialNames, const TArray<std::filesystem::path>& TexturePaths)
	{
		const TMap<FString, bool> TextureBackedLookup = BuildTextureBackedDynamicMaterialLookup(TexturePaths);
		TArray<FString>           FilteredNames;
		for (const FString& MaterialName : MaterialNames)
		{
			if (TextureBackedLookup.find(MaterialName) == TextureBackedLookup.end())
			{
				FilteredNames.push_back(MaterialName);
			}
		}
		return FilteredNames;
	}

	// Materialì˜ í˜„ìž¬ Normal Texture ìŠ¬ë¡¯ì— ëŒ€í•œ ì½¤ë³´ ë°•ìŠ¤ UI.
	// None ì„ íƒ ì‹œ í•´ì œ, íŒŒì¼ ì„ íƒ ì‹œ ë””ìŠ¤í¬ì—ì„œ ë¡œë“œ. ë°˜í™˜ê°’ì€ ë³€ê²½ ì—¬ë¶€ (í˜„ìž¬ëŠ” ì‚¬ìš©í•˜ì§€ ì•ŠìŒ).
	bool DrawNormalTextureCombo(UMeshComponent* MeshComponent, int32 MaterialIndex, const std::shared_ptr<FMaterial>& CurrentMaterial)
	{
		if (!CurrentMaterial)
		{
			return false;
		}

		std::filesystem::path CurrentPath;
		if (MeshComponent)
		{
			const FString& OverridePath = MeshComponent->GetNormalTextureOverride(MaterialIndex);
			if (!OverridePath.empty())
			{
				CurrentPath = std::filesystem::path(OverridePath).lexically_normal();
			}
		}

		if (CurrentPath.empty())
		{
			if (std::shared_ptr<FMaterialTexture> NormalTexture = CurrentMaterial->GetNormalTexture())
			{
				if (!NormalTexture->SourcePath.empty())
				{
					CurrentPath = std::filesystem::path(NormalTexture->SourcePath).lexically_normal();
				}
			}
		}

		const bool        bHasNormal   = CurrentMaterial->HasNormalTexture();
		const std::string CurrentLabel = !CurrentPath.empty()
			                                 ? CurrentPath.filename().string()
			                                 : (bHasNormal ? "(Custom)" : "None");
		bool bChanged = false;

		if (ImGui::BeginCombo("Normal Texture", CurrentLabel.c_str()))
		{
			const bool bNoneSelected = CurrentPath.empty() && !bHasNormal;
			if (ImGui::Selectable("None", bNoneSelected))
			{
				if (MeshComponent)
				{
					MeshComponent->ClearNormalTextureOverride(MaterialIndex);
				}
				else
				{
					ClearNormalTexture(CurrentMaterial);
				}
				bChanged = true;
			}

			for (const std::filesystem::path& Path : GetAvailableTexturePaths())
			{
				const std::string           Name           = Path.filename().string();
				const std::filesystem::path NormalizedPath = Path.lexically_normal();
				const bool                  bSelected      = (!CurrentPath.empty() && NormalizedPath == CurrentPath);
				const std::string           PathId         = NormalizedPath.string();
				ImGui::PushID(PathId.c_str());
				if (ImGui::Selectable(Name.c_str(), bSelected))
				{
					if (MeshComponent)
					{
						MeshComponent->SetNormalTextureOverride(MaterialIndex, Path.string());
					}
					else
					{
						LoadNormalTextureFromFile(CurrentMaterial, Path);
					}
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	FShowFlags BuildMaterialPreviewShowFlags()
	{
		FShowFlags ShowFlags;
		ShowFlags.SetFlag(EEngineShowFlags::SF_UUID, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_Billboard, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_Text, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_Fog, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_Decal, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_DecalArrow, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_ProjectileArrow, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_WorldAxis, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, false);
		ShowFlags.SetFlag(EEngineShowFlags::SF_FXAA, true);
		return ShowFlags;
	}

	void EnsureActorComponentRegistered(AActor* OwnerActor, UActorComponent* Component)
	{
		if (!OwnerActor || !Component)
		{
			return;
		}

		OwnerActor->AddOwnedComponent(Component);
		if (Component->IsA(USceneComponent::StaticClass()) && OwnerActor->GetRootComponent() == nullptr)
		{
			OwnerActor->SetRootComponent(static_cast<USceneComponent*>(Component));
		}

		if (!Component->IsRegistered())
		{
			Component->OnRegister();
		}

		if (Component->IsA(UPrimitiveComponent::StaticClass()))
		{
			static_cast<UPrimitiveComponent*>(Component)->UpdateBounds();
		}
	}

	USceneComponent* GetComponentTreeParentSceneComponent(UActorComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}

		if (Component->IsA(USceneComponent::StaticClass()))
		{
			return static_cast<USceneComponent*>(Component)->GetAttachParent();
		}

		if (Component->IsA(UMovementComponent::StaticClass()))
		{
			return static_cast<UMovementComponent*>(Component)->GetUpdatedComponent();
		}

		return nullptr;
	}

	bool ShouldDrawUnderSceneComponent(UActorComponent* Component, USceneComponent* SceneComponent)
	{
		if (!Component || !SceneComponent || Component->IsA(USceneComponent::StaticClass()))
		{
			return false;
		}

		if (Component->IsA(UMovementComponent::StaticClass()))
		{
			return static_cast<UMovementComponent*>(Component)->GetUpdatedComponent() == SceneComponent;
		}

		return false;
	}

	bool HasAttachedNonSceneChildren(USceneComponent* SceneComponent)
	{
		if (!SceneComponent)
		{
			return false;
		}

		AActor* OwnerActor = SceneComponent->GetOwner();
		if (!OwnerActor)
		{
			return false;
		}

		for (UActorComponent* Component : OwnerActor->GetComponents())
		{
			if (IsHiddenEditorOnlyComponent(Component))
			{
				continue;
			}

			if (ShouldDrawUnderSceneComponent(Component, SceneComponent))
			{
				return true;
			}
		}

		return false;
	}

	void CollectTextureFiles(const std::filesystem::path& Root, TArray<std::filesystem::path>& OutFiles)
	{
		OutFiles.clear();
		if (!std::filesystem::exists(Root))
		{
			return;
		}

		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			if (IsSupportedTextureFile(Entry.path()))
			{
				OutFiles.push_back(Entry.path());
			}
		}

		std::sort(OutFiles.begin(), OutFiles.end());
	}

	const ELightUnits GLightUnitOrder[] =
	{
		ELightUnits::Unitless,
		ELightUnits::Candelas,
		ELightUnits::Lumens,
		ELightUnits::Lux,
	};

	const char* GetLightUnitLabel(ELightUnits Unit)
	{
		switch (Unit)
		{
		case ELightUnits::Unitless:
			return "Unitless";
		case ELightUnits::Candelas:
			return "Candelas (cd)";
		case ELightUnits::Lumens:
			return "Lumens (lm)";
		case ELightUnits::Lux:
			return "Lux (lx)";
		default:
			return "Unknown";
		}
	}

	std::string GetMeshDisplayName(const UStaticMesh* Mesh)
	{
		if (!Mesh)
		{
			return "None";
		}

		const std::string AssetName = Mesh->GetAssetPathFileName();
		if (!AssetName.empty())
		{
			return AssetName;
		}

		const std::string ObjectName = Mesh->GetName();
		if (!ObjectName.empty())
		{
			return ObjectName;
		}

		return "UnnamedMesh";
	}

	template <typename TComponent>
	TComponent* FindOwnedComponentByExactName(AActor* OwnerActor, const FString& ComponentName)
	{
		if (!OwnerActor || ComponentName.empty())
		{
			return nullptr;
		}

		for (UActorComponent* Component : OwnerActor->GetComponents())
		{
			if (!Component || !Component->IsA(TComponent::StaticClass()) || Component->GetName() != ComponentName)
			{
				continue;
			}

			return static_cast<TComponent*>(Component);
		}

		return nullptr;
	}

	FString GetLightDebugBillboardName(const ULightComponent* LightComponent)
	{
		return LightComponent ? (LightComponent->GetName() + "_DebugBillboard") : FString();
	}

	FString GetPointLightDebugGizmoName(const UPointLightComponent* PointLightComponent)
	{
		return PointLightComponent ? (PointLightComponent->GetName() + "_PointRadiusGizmo") : FString();
	}

	FString GetSpotLightDebugGizmoName(const USpotLightComponent* SpotLightComponent)
	{
		return SpotLightComponent ? (SpotLightComponent->GetName() + "_SpotConeGizmo") : FString();
	}

	bool IsActorSelectedInEditor(AActor* Actor)
	{
		if (!Actor || !GEngine)
		{
			return true;
		}

		auto EditorEngine = static_cast<FEditorEngine*>(GEngine);
		if (!EditorEngine)
		{
			return true;
		}
		return EditorEngine->IsActorSelected(Actor);
	}

	void RefreshAttachedLightBillboard(ULightComponent* LightComponent)
	{
		if (!LightComponent)
		{
			return;
		}

		AActor* OwnerActor = LightComponent->GetOwner();
		if (!OwnerActor)
		{
			return;
		}

		UBillboardComponent* BillboardComponent =
				FindOwnedComponentByExactName<UBillboardComponent>(OwnerActor, GetLightDebugBillboardName(LightComponent));
		if (!BillboardComponent)
		{
			return;
		}

		FLinearColor Tint = LightComponent->GetColor();
		Tint.A            = 1.0f;
		BillboardComponent->SetBaseColorLinear(Tint);
	}

	void RefreshAttachedPointLightGizmo(UPointLightComponent* PointLightComponent)
	{
		if (!PointLightComponent)
		{
			return;
		}

		AActor* OwnerActor = PointLightComponent->GetOwner();
		if (!OwnerActor)
		{
			return;
		}

		ULineBatchComponent* RadiusGizmoComponent =
				FindOwnedComponentByExactName<ULineBatchComponent>(OwnerActor, GetPointLightDebugGizmoName(PointLightComponent));
		if (!RadiusGizmoComponent)
		{
			return;
		}

		const bool bIsSelected = IsActorSelectedInEditor(OwnerActor);
		RadiusGizmoComponent->SetEditorVisualization(bIsSelected);

		RadiusGizmoComponent->Clear();
		const float Radius = (std::max)(PointLightComponent->GetAttenuationRadius(), 0.0f);
		if (Radius <= FMath::SmallNumber)
		{
			return;
		}

		const FVector4 GizmoColor(0.10f, 0.45f, 1.00f, 1.00f);
		RadiusGizmoComponent->DrawWireSphere(FVector::ZeroVector, Radius, GizmoColor);
	}

	void RefreshAttachedSpotLightGizmo(USpotLightComponent* SpotLightComponent)
	{
		if (!SpotLightComponent)
		{
			return;
		}

		AActor* OwnerActor = SpotLightComponent->GetOwner();
		if (!OwnerActor)
		{
			return;
		}

		ULineBatchComponent* ConeGizmoComponent =
				FindOwnedComponentByExactName<ULineBatchComponent>(OwnerActor, GetSpotLightDebugGizmoName(SpotLightComponent));
		if (!ConeGizmoComponent)
		{
			return;
		}

		const bool bIsSelected = IsActorSelectedInEditor(OwnerActor);
		ConeGizmoComponent->SetEditorVisualization(bIsSelected);

		ConeGizmoComponent->Clear();

		const float Length = (std::max)(SpotLightComponent->GetAttenuationRadius(), 0.0f);
		if (Length <= FMath::SmallNumber)
		{
			return;
		}

		const float OuterConeAngle = FMath::Clamp(SpotLightComponent->GetOuterConeAngle(), 0.0f, 80.0f);
		const float InnerConeAngle = FMath::Clamp(SpotLightComponent->GetInnerConeAngle(), 0.0f, OuterConeAngle);

		const FVector4 OuterColor(0.20f, 0.75f, 1.00f, 1.00f);
		const FVector4 InnerColor(0.05f, 0.35f, 0.85f, 1.00f);

		ConeGizmoComponent->DrawWireCone(
			FVector::ZeroVector,
			FVector::ForwardVector,
			Length,
			OuterConeAngle,
			OuterColor,
			16,
			24,
			true);

		if (InnerConeAngle > FMath::KindaSmallNumber)
		{
			ConeGizmoComponent->DrawWireCone(
				FVector::ZeroVector,
				FVector::ForwardVector,
				Length,
				InnerConeAngle,
				InnerColor,
				16,
				16,
				true);
		}
	}

	void EnsureLightDebugAttachments(UActorComponent* NewComponent)
	{
		if (!NewComponent || !NewComponent->IsA(ULightComponent::StaticClass()))
		{
			return;
		}

		auto    LightComponent = static_cast<ULightComponent*>(NewComponent);
		AActor* OwnerActor     = LightComponent->GetOwner();
		if (!OwnerActor)
		{
			return;
		}

		UBillboardComponent* BillboardComponent =
				FindOwnedComponentByExactName<UBillboardComponent>(OwnerActor, GetLightDebugBillboardName(LightComponent));
		if (!BillboardComponent)
		{
			BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(
				OwnerActor,
				GetLightDebugBillboardName(LightComponent));
			if (BillboardComponent)
			{
				BillboardComponent->SetInstanceComponent(true);
				OwnerActor->AddOwnedComponent(BillboardComponent);
				if (!BillboardComponent->IsRegistered())
				{
					BillboardComponent->OnRegister();
				}
			}
		}

		if (BillboardComponent)
		{
			BillboardComponent->AttachTo(LightComponent);
			BillboardComponent->SetIgnoreParentScaleInRender(true);
			BillboardComponent->SetEditorVisualization(true);
			BillboardComponent->SetHiddenInGame(true);

			if (LightComponent->IsA(USpotLightComponent::StaticClass()))
			{
				BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightSpot.png").wstring());
				BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
			}
			else if (LightComponent->IsA(UPointLightComponent::StaticClass()))
			{
				BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightPoint.png").wstring());
				BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
			}
			else if (LightComponent->IsA(UDirectionalLightComponent::StaticClass()))
			{
				BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightDirectional.png").wstring());
				BillboardComponent->SetSize(FVector2(0.7f, 0.7f));
			}
			else if (LightComponent->IsA(UAmbientLightComponent::StaticClass()))
			{
				BillboardComponent->SetTexturePath((FPaths::IconDir() / L"SkyLight.png").wstring());
				BillboardComponent->SetSize(FVector2(0.7f, 0.7f));
			}
		}

		RefreshAttachedLightBillboard(LightComponent);

		if (LightComponent->IsA(USpotLightComponent::StaticClass()))
		{
			auto                 SpotLightComponent = static_cast<USpotLightComponent*>(LightComponent);
			ULineBatchComponent* ConeGizmoComponent =
					FindOwnedComponentByExactName<ULineBatchComponent>(OwnerActor, GetSpotLightDebugGizmoName(SpotLightComponent));
			if (!ConeGizmoComponent)
			{
				ConeGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(
					OwnerActor,
					GetSpotLightDebugGizmoName(SpotLightComponent));
				if (ConeGizmoComponent)
				{
					ConeGizmoComponent->SetInstanceComponent(true);
					OwnerActor->AddOwnedComponent(ConeGizmoComponent);
					if (!ConeGizmoComponent->IsRegistered())
					{
						ConeGizmoComponent->OnRegister();
					}
				}
			}

			if (ConeGizmoComponent)
			{
				ConeGizmoComponent->AttachTo(SpotLightComponent);
				ConeGizmoComponent->SetIgnoreParentScaleInRender(true);
				ConeGizmoComponent->SetEditorVisualization(true);
				ConeGizmoComponent->SetHiddenInGame(true);
				ConeGizmoComponent->SetDrawDebugBounds(false);
			}

			RefreshAttachedSpotLightGizmo(SpotLightComponent);
		}
		else if (LightComponent->IsA(UPointLightComponent::StaticClass()))
		{
			auto                 PointLightComponent  = static_cast<UPointLightComponent*>(LightComponent);
			ULineBatchComponent* RadiusGizmoComponent =
					FindOwnedComponentByExactName<ULineBatchComponent>(OwnerActor, GetPointLightDebugGizmoName(PointLightComponent));
			if (!RadiusGizmoComponent)
			{
				RadiusGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(
					OwnerActor,
					GetPointLightDebugGizmoName(PointLightComponent));
				if (RadiusGizmoComponent)
				{
					RadiusGizmoComponent->SetInstanceComponent(true);
					OwnerActor->AddOwnedComponent(RadiusGizmoComponent);
					if (!RadiusGizmoComponent->IsRegistered())
					{
						RadiusGizmoComponent->OnRegister();
					}
				}
			}

			if (RadiusGizmoComponent)
			{
				RadiusGizmoComponent->AttachTo(PointLightComponent);
				RadiusGizmoComponent->SetIgnoreParentScaleInRender(true);
				RadiusGizmoComponent->SetEditorVisualization(true);
				RadiusGizmoComponent->SetHiddenInGame(true);
				RadiusGizmoComponent->SetDrawDebugBounds(false);
			}

			RefreshAttachedPointLightGizmo(PointLightComponent);
		}
	}
}

FPropertyWindow::~FPropertyWindow()
{
	if (MaterialPreviewTargetManager)
	{
		MaterialPreviewTargetManager->Release();
	}

	if (MaterialPreviewViewport)
	{
		MaterialPreviewViewport->Release();
	}
}

void FPropertyWindow::Render(FEditorEngine* Engine)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	bool bOpen = ImGui::Begin("Properties");
	ImGui::PopStyleVar();

	if (!bOpen)
	{
		ImGui::End();
		return;
	}

	bModified = false;

	ImGui::TextDisabled("Selected:");
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.4f, 1.0f), "%s", ActorNameBuf);

	AActor* EditorSelectedActor = Engine ? Engine->GetSelectedActor() : nullptr;
	AActor* SelectedActor = ResolvePropertyActorForLightDebug(Engine, EditorSelectedActor);
	if (SelectedActor != LastSelectedActor)
	{
		ImGui::ClearActiveID();
	}

	if (!IsComponentOwnedByActor(SelectedActor, SelectedComponent))
	{
		SelectedComponent = nullptr;
		if (SelectedActor)
		{
			if (USceneComponent* RootComponent = SelectedActor->GetRootComponent())
			{
				SelectedComponent = RootComponent;
			}
			else
			{
				for (UActorComponent* Component : SelectedActor->GetComponents())
				{
					if (Component && !IsHiddenEditorOnlyComponent(Component))
					{
						SelectedComponent = Component;
						break;
					}
				}
			}
		}
	}

	if (SelectedComponent != LastSelectedComponent)
	{
		ImGui::ClearActiveID();
	}

	ImGui::Separator();

	ImGui::TextDisabled("Components");
	DrawAddComponentButton(SelectedActor);

	const float AvailableHeight       = ImGui::GetContentRegionAvail().y;
	const float ComponentsPanelHeight = AvailableHeight > 220.0f ? AvailableHeight * 0.4f : 120.0f;
	if (ImGui::BeginChild("##ComponentsPanel", ImVec2(0.0f, ComponentsPanelHeight), true))
	{
		DrawComponentSection(SelectedActor);
	}
	ImGui::EndChild();

	ImGui::Spacing();
	ImGui::TextDisabled("Details");
	if (ImGui::BeginChild("##DetailsPanel", ImVec2(0.0f, 0.0f), true))
	{
		DrawDetailsSection(SelectedComponent, Engine);
	}
	ImGui::EndChild();

	LastSelectedActor     = SelectedActor;
	LastSelectedComponent = SelectedComponent;

	ImGui::End();
	return;

	if (SelectedActor && ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);
		DrawComponentSection(SelectedActor);
		ImGui::Unindent(8.0f);
	}

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);
		DrawTransformSection();
		ImGui::Unindent(8.0f);
	}
	if (Engine)
	{
		if (SelectedActor)
		{
			if (ImGui::CollapsingHeader("Billboard", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent(8.0f);
				for (UActorComponent* Component : SelectedActor->GetComponents())
				{
					if (!Component)
					{
						continue;
					}

					if (Component->IsA(USubUVComponent::StaticClass()))
					{
						auto SubUVComp  = static_cast<USubUVComponent*>(Component);
						bool bBillboard = SubUVComp->IsBillboard();
						if (ImGui::Checkbox("SubUV Billboard", &bBillboard))
						{
							SubUVComp->SetBillboard(bBillboard);
						}
					}
					else if (Component->IsA(UTextRenderComponent::StaticClass()) && !Component->IsA(UUUIDBillboardComponent::StaticClass()))
					{
						auto TextComp   = static_cast<UTextRenderComponent*>(Component);
						bool bBillboard = TextComp->IsBillboard();
						if (ImGui::Checkbox("Text Billboard", &bBillboard))
						{
							TextComp->SetBillboard(bBillboard);
						}
					}
				}
				ImGui::Unindent(8.0f);
			}
			if (UStaticMeshComponent* MeshComp = SelectedActor->GetComponentByClass<UStaticMeshComponent>())
			{
				if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Indent(8.0f);

					// 1. 현재 컴포넌트에 할당된 메쉬 정보 가져오기
					UStaticMesh* CurrentMesh     = MeshComp->GetStaticMesh();
					std::string  CurrentMeshName = CurrentMesh ? CurrentMesh->GetAssetPathFileName() : "None";

					ImGui::Text("Mesh Asset:");
					ImGui::SameLine();

					ImGui::PushItemWidth(200.f);
					if (ImGui::BeginCombo("##StaticMeshAssign", CurrentMeshName.c_str()))
					{
						// 2. TObjectIterator를 사용하여 로드된 모든 UStaticMesh를 순회
						for (TObjectIterator<UStaticMesh> It; It; ++It)
						{
							UStaticMesh* MeshAsset = It.Get();
							if (!MeshAsset)
							{
								continue;
							}

							std::string MeshName  = MeshAsset->GetAssetPathFileName();
							bool        bSelected = (CurrentMesh == MeshAsset);

							if (ImGui::Selectable(MeshName.c_str(), bSelected))
							{
								// 3. 선택 시 새로운 메쉬 할당
								MeshComp->SetStaticMesh(MeshAsset);
							}

							if (bSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopItemWidth();

					ImGui::Unindent(8.0f);
				}

				if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Indent(8.0f);

					if (UStaticMesh* MeshData = MeshComp->GetStaticMesh())
					{
						// 매니저에서 모든 머티리얼 리스트 가져오기
						const TArray<std::filesystem::path> TexturePaths = GetAvailableTexturePaths();
						TArray<FString>                     MatNames     = BuildMaterialPickerNames(FMaterialManager::Get().GetAllMaterialNames(), TexturePaths);
						uint32                              NumSections  = MeshData->GetNumSections();

						// ========================================================
						// [기능 1] 전체 섹션 머티리얼 일괄 변경
						// ========================================================
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
						ImGui::Text("Apply to All Sections:");
						ImGui::PopStyleColor();
						ImGui::SameLine();

						ImGui::PushItemWidth(180.f);
						if (ImGui::BeginCombo("##SetAllMaterials", "Select Material..."))
						{
							for (const FString& MatName : MatNames)
							{
								ImGui::PushID(MatName.c_str());

								auto        ListMaterial = FMaterialManager::Get().FindByName(MatName);
								ImTextureID TexID        = (ImTextureID)0; // 빨간줄 방지용 0 캐스팅

								if (ListMaterial && ListMaterial->GetMaterialTexture() && ListMaterial->GetMaterialTexture()->TextureSRV)
								{
									TexID = (ImTextureID)ListMaterial->GetMaterialTexture()->TextureSRV;
								}

								// 텍스처가 있으면 리스트에 썸네일 렌더링
								if (TexID)
								{
									ImGui::Image(TexID, ImVec2(24.0f, 24.0f));
									ImGui::SameLine();
									ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f); // 텍스트와 높이 맞춤
								}

								if (ImGui::Selectable(MatName.c_str(), false))
								{
									if (ListMaterial)
									{
										for (uint32 j = 0; j < NumSections; ++j)
										{
											MeshComp->SetMaterial(j, ListMaterial);
										}
									}
								}
								ImGui::PopID();
							}
							if (!TexturePaths.empty())
							{
								ImGui::SeparatorText("Textures");
								for (const std::filesystem::path& TexturePath : TexturePaths)
								{
									const FString TextureMaterialName = GetTextureMaterialName(TexturePath);
									ImGui::PushID(TextureMaterialName.c_str());
									if (ImGui::Selectable(TextureMaterialName.c_str(), false))
									{
										if (std::shared_ptr<FMaterial> TextureMaterial = LoadTextureMaterial(TexturePath))
										{
											for (uint32 j = 0; j < NumSections; ++j)
											{
												MeshComp->SetMaterial(j, TextureMaterial);
											}
										}
									}
									ImGui::PopID();
								}
							}
							ImGui::EndCombo();
						}
						ImGui::PopItemWidth();

						float MasterScroll[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

						if (NumSections > 0)
						{
							if (std::shared_ptr<FMaterial> FirstMat = MeshComp->GetMaterial(0))
							{
								FirstMat->GetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
							}
						}

						ImGui::PushItemWidth(180.f);
						// DragFloat2를 사용하므로 MasterScroll[0], MasterScroll[1] 값만 조작됩니다. (나머지 2개는 패딩 역할)
						if (ImGui::DragFloat2("Scroll All Sections", MasterScroll, 0.001f, -5.0f, 5.0f, "%.2f"))
						{
							for (uint32 j = 0; j < NumSections; ++j)
							{
								if (std::shared_ptr<FMaterial> Mat = MeshComp->GetMaterial(j))
								{
									Mat->SetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
								}
							}
						}
						ImGui::PopItemWidth();

						ImGui::Separator();
						ImGui::Spacing();
						// ========================================================

						// 섹션 개수만큼 머티리얼 슬롯(콤보박스) 생성
						for (uint32 i = 0; i < NumSections; ++i)
						{
							std::shared_ptr<FMaterial> CurrentMat     = MeshComp->GetMaterial(i);
							std::string                CurrentMatName = CurrentMat ? CurrentMat->GetOriginName() : "None";

							ImGui::PushID(i); // ID 충돌 방지
							std::string Label = "Section " + std::to_string(i);

							ImGui::PushItemWidth(180.f); // 콤보박스 너비 조절

							// ========================================================
							// [기능 2] 개별 섹션 콤보박스 오픈 시 미리보기 출력
							// ========================================================
							if (ImGui::BeginCombo(Label.c_str(), CurrentMatName.c_str()))
							{
								for (const FString& MatName : MatNames)
								{
									ImGui::PushID(MatName.c_str());
									bool bSelected = (CurrentMatName == MatName);

									auto        ListMaterial = FMaterialManager::Get().FindByName(MatName);
									ImTextureID TexID        = (ImTextureID)0; // 빨간줄 방지용 0 캐스팅

									if (ListMaterial && ListMaterial->GetMaterialTexture() && ListMaterial->GetMaterialTexture()->TextureSRV)
									{
										TexID = (ImTextureID)ListMaterial->GetMaterialTexture()->TextureSRV;
									}

									// 텍스처가 있으면 리스트에 썸네일 렌더링
									if (TexID)
									{
										ImGui::Image(TexID, ImVec2(24.0f, 24.0f));
										ImGui::SameLine();
										ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f); // 텍스트와 높이 맞춤
									}

									if (ImGui::Selectable(MatName.c_str(), bSelected))
									{
										if (ListMaterial)
										{
											MeshComp->SetMaterial(i, ListMaterial);
										}
									}
									if (bSelected)
									{
										ImGui::SetItemDefaultFocus();
									}
									ImGui::PopID();
								}
								if (!TexturePaths.empty())
								{
									ImGui::SeparatorText("Textures");
									for (const std::filesystem::path& TexturePath : TexturePaths)
									{
										const FString TextureMaterialName = GetTextureMaterialName(TexturePath);
										const bool    bSelected           = (CurrentMatName == TextureMaterialName);
										ImGui::PushID(TextureMaterialName.c_str());
										if (ImGui::Selectable(TextureMaterialName.c_str(), bSelected))
										{
											if (std::shared_ptr<FMaterial> TextureMaterial = LoadTextureMaterial(TexturePath))
											{
												MeshComp->SetMaterial(i, TextureMaterial);
												CurrentMat     = MeshComp->GetMaterial(i);
												CurrentMatName = CurrentMat ? CurrentMat->GetOriginName() : "None";
											}
										}
										if (bSelected)
										{
											ImGui::SetItemDefaultFocus();
										}
										ImGui::PopID();
									}
								}
								ImGui::EndCombo();
							}

							if (CurrentMat)
							{
								FLinearColor MatColor(CurrentMat->GetVectorParameter("BaseColor"));

								ImGui::PushID(i + 1000);
								if (EditLinearColor4("Base Color", MatColor))
								{
									CurrentMat->SetLinearColorParameter("BaseColor", MatColor);
								}
								ImGui::PopID();

								if (auto MatTex = CurrentMat->GetMaterialTexture())
								{
									float SpeedArray[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
									CurrentMat->GetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));

									ImGui::PushID(i + 2000);
									// 마찬가지로 UI 조작은 X, Y 2개만 합니다.
									if (ImGui::DragFloat2("UV Scroll", SpeedArray, 0.001f, -5.0f, 5.0f, "%.2f"))
									{
										CurrentMat->SetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));
									}
									ImGui::PopID();
								}

								ImGui::PushID(i + 3000);
								DrawNormalTextureCombo(MeshComp, static_cast<int32>(i), CurrentMat);
								ImGui::PopID();
							}
							ImGui::PopID(); // PushID(i)에 대한 Pop
							ImGui::Spacing();
						}
					}
					else
					{
						ImGui::TextDisabled("No Static Mesh Assigned");
					}
					ImGui::Unindent(8.0f);
				}
			}

			if (UBillboardComponent* BillboardComp = SelectedActor->GetComponentByClass<UBillboardComponent>())
			{
			}
		}
	}
	ImGui::End();
}

void FPropertyWindow::SetTarget(const FVector& Location,
                                const FVector& Rotation,
                                const FVector& Scale,
                                const char*    ActorName)
{
	EditLocation = Location;
	EditRotation = Rotation;
	EditScale    = Scale;
	bModified    = false;

	if (ActorName)
	{
		snprintf(ActorNameBuf, sizeof(ActorNameBuf), "%s", ActorName);
	}
	else
	{
		snprintf(ActorNameBuf, sizeof(ActorNameBuf), "None");
	}
}

void FPropertyWindow::DrawTransformSection()
{
}

void FPropertyWindow::DrawComponentSection(AActor* SelectedActor)
{
	if (!SelectedActor)
	{
		ImGui::TextDisabled("No actor selected.");
		return;
	}

	if (USceneComponent* RootComponent = SelectedActor->GetRootComponent())
	{
		DrawSceneComponentNode(RootComponent, 0);
	}
	else
	{
		ImGui::TextDisabled("No root scene component.");
	}

	bool bHasNonSceneComponent = false;
	for (UActorComponent* Component : SelectedActor->GetComponents())
	{
		if (!Component || IsHiddenEditorOnlyComponent(Component) || Component->IsA(USceneComponent::StaticClass()))
		{
			continue;
		}

		if (GetComponentTreeParentSceneComponent(Component) != nullptr)
		{
			continue;
		}

		if (!bHasNonSceneComponent)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Other Components");
			bHasNonSceneComponent = true;
		}

		DrawNonSceneComponentEntry(Component);
	}
}

void FPropertyWindow::DrawSceneComponentNode(USceneComponent* Component, int32 Depth)
{
	if (!Component)
	{
		return;
	}

	const FString ClassName                    = Component->GetClass() ? Component->GetClass()->GetName() : "USceneComponent";
	const FString ComponentName                = Component->GetName().empty() ? ClassName : Component->GetName();
	const bool    bIsRoot                      = (Component->GetAttachParent() == nullptr);
	const bool    bHasAttachedNonSceneChildren = HasAttachedNonSceneChildren(Component);
	int32         VisibleChildCount            = 0;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (!IsHiddenEditorOnlyComponent(Child))
		{
			++VisibleChildCount;
		}
	}
	const bool         bHasTreeChildren = VisibleChildCount > 0 || bHasAttachedNonSceneChildren;
	ImGuiTreeNodeFlags Flags            = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (SelectedComponent == Component)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!bHasTreeChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	const bool bOpen = ImGui::TreeNodeEx(
		Component,
		Flags,
		"%s%s (%s)",
		bIsRoot ? "[Root] " : "",
		ComponentName.c_str(),
		ClassName.c_str());

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		SelectedComponent = Component;
	}

	if (bOpen)
	{
		for (USceneComponent* Child : Component->GetAttachChildren())
		{
			if (IsHiddenEditorOnlyComponent(Child))
			{
				continue;
			}

			DrawSceneComponentNode(Child, Depth + 1);
		}

		DrawAttachedNonSceneComponentNodes(Component, Depth + 1);

		if (bHasTreeChildren)
		{
			ImGui::TreePop();
		}
	}
}

void FPropertyWindow::DrawAttachedNonSceneComponentNodes(USceneComponent* SceneComponent, int32 Depth)
{
	if (!SceneComponent)
	{
		return;
	}

	AActor* OwnerActor = SceneComponent->GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		if (!ShouldDrawUnderSceneComponent(Component, SceneComponent))
		{
			continue;
		}

		DrawNonSceneComponentEntry(Component);
	}
}

void FPropertyWindow::DrawNonSceneComponentEntry(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	const FString      ClassName     = Component->GetClass() ? Component->GetClass()->GetName() : "UActorComponent";
	const FString      ComponentName = Component->GetName().empty() ? ClassName : Component->GetName();
	ImGuiTreeNodeFlags Flags         = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (SelectedComponent == Component)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::TreeNodeEx(
		Component,
		Flags,
		"%s (%s)",
		ComponentName.c_str(),
		ClassName.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		SelectedComponent = Component;
	}
}

void FPropertyWindow::DrawDetailsSection(UActorComponent* Component, FEditorEngine* Engine)
{
	if (!Component)
	{
		ImGui::TextDisabled("Select a component from the Components panel.");
		return;
	}

	const FString ClassName     = Component->GetClass() ? Component->GetClass()->GetName() : "UActorComponent";
	const FString ComponentName = Component->GetName().empty() ? ClassName : Component->GetName();
	ImGui::PushID(Component);

	ImGui::Text("Name: %s", ComponentName.c_str());
	ImGui::Text("Class: %s", ClassName.c_str());
	ImGui::Text("Registered: %s", Component->IsRegistered() ? "Yes" : "No");

	if (AActor* OwnerActor = Component->GetOwner())
	{
		const bool bCanDelete = OwnerActor->CanDeleteInstanceComponent(Component);
		ImGui::BeginDisabled(!bCanDelete);
		if (ImGui::Button("Delete Component"))
		{
			USceneComponent* ParentSceneComponent = GetComponentTreeParentSceneComponent(Component);

			if (OwnerActor->DestroyInstanceComponent(Component))
			{
				if (ParentSceneComponent && IsComponentOwnedByActor(OwnerActor, ParentSceneComponent))
				{
					SelectedComponent = ParentSceneComponent;
				}
				else if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
				{
					SelectedComponent = RootComponent;
				}
				else
				{
					SelectedComponent = nullptr;
					for (UActorComponent* RemainingComponent : OwnerActor->GetComponents())
					{
						if (RemainingComponent)
						{
							SelectedComponent = RemainingComponent;
							break;
						}
					}
				}

				ImGui::EndDisabled();
				ImGui::PopID();
				return;
			}
		}
		ImGui::EndDisabled();
	}

	bool bTickEnabled = Component->IsComponentTickEnabled();
	if (ImGui::Checkbox("Tick Enabled", &bTickEnabled))
	{
		Component->SetComponentTickEnabled(bTickEnabled);
	}

	if (Component->IsA(USceneComponent::StaticClass()))
	{
		DrawSceneComponentDetails(static_cast<USceneComponent*>(Component));
	}

	if (Component->IsA(UMovementComponent::StaticClass()))
	{
		DrawMovementComponentDetails(static_cast<UMovementComponent*>(Component));
	}

	if (Component->IsA(URotatingMovementComponent::StaticClass()))
	{
		DrawRotatingMovementComponentDetails(static_cast<URotatingMovementComponent*>(Component));
	}

	if (Component->IsA(UProjectileMovementComponent::StaticClass()))
	{
		DrawProjectileMovementComponentDetails(static_cast<UProjectileMovementComponent*>(Component), Engine);
	}

	if (Component->IsA(USpringArmComponent::StaticClass()))
	{
		DrawSpringArmComponentDetails(static_cast<USpringArmComponent*>(Component));
	}

	if (Component->IsA(UStaticMeshComponent::StaticClass()))
	{
		DrawStaticMeshComponentDetails(static_cast<UStaticMeshComponent*>(Component), Engine);
	}

	if (Component->IsA(UTextRenderComponent::StaticClass()) && !Component->IsA(UUUIDBillboardComponent::StaticClass()))
	{
		DrawTextComponentDetails(static_cast<UTextRenderComponent*>(Component));
	}

	if (Component->IsA(USubUVComponent::StaticClass()))
	{
		DrawSubUVComponentDetails(static_cast<USubUVComponent*>(Component));
	}

	if (Component->IsA(UHeightFogComponent::StaticClass()))
	{
		DrawHeightFogComponentDetails(static_cast<UHeightFogComponent*>(Component));
	}

	if (Component->IsA(ULocalHeightFogComponent::StaticClass()))
	{
		DrawLocalHeightFogComponentDetails(static_cast<ULocalHeightFogComponent*>(Component));
	}

	if (Component->IsA(UBillboardComponent::StaticClass()))
	{
		DrawBillboardComponentDetials(static_cast<UBillboardComponent*>(Component), Engine);
	}

	if (Component->IsA(UMeshDecalComponent::StaticClass()))
	{
		DrawMeshDecalComponentDetails(static_cast<UMeshDecalComponent*>(Component), Engine);
	}
	else if (Component->IsA(UDecalComponent::StaticClass()))
	{
		DrawDecalComponentDetails(static_cast<UDecalComponent*>(Component), Engine);
	}

	if (Component->IsA(UFireBallComponent::StaticClass()))
	{
		DrawFireBallComponentDetails(static_cast<UFireBallComponent*>(Component));
	}

	if (Component->IsA(ULightComponent::StaticClass()))
	{
		DrawLightComponentDetails(static_cast<ULightComponent*>(Component), Engine);
	}

	if (Component->IsA(USpotLightComponent::StaticClass()))
	{
		DrawPointLightComponentDetails(static_cast<UPointLightComponent*>(Component), false);
		DrawSpotLightComponentDetails(static_cast<USpotLightComponent*>(Component));
	}
	else if (Component->IsA(UPointLightComponent::StaticClass()))
	{
		DrawPointLightComponentDetails(static_cast<UPointLightComponent*>(Component), true);
	}
	else if (Component->IsA(UDirectionalLightComponent::StaticClass()))
	{
		DrawDirectionalLightComponentDetails(static_cast<UDirectionalLightComponent*>(Component));
	}

	ImGui::PopID();
}

void FPropertyWindow::DrawSceneComponentDetails(USceneComponent* SceneComponent)
{
	if (!SceneComponent)
	{
		return;
	}

	ImGui::TextDisabled("Transform");

	FTransform RelativeTransform = SceneComponent->GetRelativeTransform();
	bool       bChangedTransform = false;

	FVector NewLocation = RelativeTransform.GetTranslation();
	ImGui::Text("Location");
	ImGui::NextColumn();
	if (DrawVector3Control("Location", RelativeTransform.GetTranslation(), NewLocation, 0.1f, "%.2f"))
	{
		RelativeTransform.SetTranslation(NewLocation);
		bChangedTransform = true;
	}

	const FVector CurrentEuler = RelativeTransform.Rotator().Euler();
	ImGui::Text("Rotation");
	ImGui::NextColumn();
	FVector NewEuler = CurrentEuler;
	if (DrawVector3Control("Rotation", CurrentEuler, NewEuler, 0.5f, "%.1f"))
	{
		RelativeTransform.SetRotation(FRotator::MakeFromEuler(NewEuler));
		bChangedTransform = true;
	}

	FVector NewScale = RelativeTransform.GetScale3D();
	ImGui::Text("Scale");
	ImGui::NextColumn();
	if (DrawVector3Control("Scale", RelativeTransform.GetScale3D(), NewScale, 0.01f, "%.3f"))
	{
		RelativeTransform.SetScale3D(NewScale);
		bChangedTransform = true;
	}

	if (bChangedTransform)
	{
		SceneComponent->SetRelativeTransform(RelativeTransform);
		RefreshSceneComponentHierarchy(SceneComponent);
		if (AActor* Owner = SceneComponent->GetOwner())
		{
			if (ULevel* Level = Owner->GetLevel())
			{
				Level->MarkSpatialDirty();
			}
		}
	}
}

void FPropertyWindow::DrawMovementComponentDetails(UMovementComponent* MovementComponent)
{
	if (!MovementComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Movement");

	AActor* OwnerActor   = MovementComponent->GetOwner();
	auto    CurrentLabel = "None";
	FString SelectedName;
	if (USceneComponent* UpdatedComponent = MovementComponent->GetUpdatedComponent())
	{
		SelectedName = UpdatedComponent->GetName().empty() ? "SceneComponent" : UpdatedComponent->GetName();
		CurrentLabel = SelectedName.c_str();
	}

	ImGui::PushItemWidth(-1.0f);
	if (ImGui::BeginCombo("Updated Component", CurrentLabel))
	{
		bool bSelectedRootFallback = (MovementComponent->GetUpdatedComponent() == nullptr);
		if (ImGui::Selectable("Root Component (Auto)", bSelectedRootFallback))
		{
			MovementComponent->SetUpdatedComponent(nullptr);
		}
		if (bSelectedRootFallback)
		{
			ImGui::SetItemDefaultFocus();
		}

		if (OwnerActor)
		{
			for (UActorComponent* Component : OwnerActor->GetComponents())
			{
				if (!Component || !Component->IsA(USceneComponent::StaticClass()))
				{
					continue;
				}

				auto          SceneComponent     = static_cast<USceneComponent*>(Component);
				const FString SceneComponentName = SceneComponent->GetName().empty()
					                                   ? SceneComponent->GetClass()->GetName()
					                                   : SceneComponent->GetName();
				const bool bSelected = (MovementComponent->GetUpdatedComponent() == SceneComponent);
				if (ImGui::Selectable(SceneComponentName.c_str(), bSelected))
				{
					MovementComponent->SetUpdatedComponent(SceneComponent);
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}

		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();
}

void FPropertyWindow::DrawStaticMeshComponentDetails(UStaticMeshComponent* MeshComponent, FEditorEngine* Engine)
{
	if (!MeshComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Static Mesh");

	UStaticMesh*      CurrentMesh     = MeshComponent->GetStaticMesh();
	const std::string CurrentMeshName = GetMeshDisplayName(CurrentMesh);

	ImGui::PushItemWidth(-1.0f);
	if (ImGui::BeginCombo("Mesh Asset", CurrentMeshName.c_str()))
	{
		for (TObjectIterator<UStaticMesh> It; It; ++It)
		{
			UStaticMesh* MeshAsset = It.Get();
			if (!MeshAsset)
			{
				continue;
			}

			const std::string MeshName  = GetMeshDisplayName(MeshAsset);
			const std::string MeshLabel = MeshName + "##MeshAsset_" + std::to_string(reinterpret_cast<uintptr_t>(MeshAsset));
			const bool        bSelected = (CurrentMesh == MeshAsset);
			if (ImGui::Selectable(MeshLabel.c_str(), bSelected))
			{
				MeshComponent->SetStaticMesh(MeshAsset);
				MeshComponent->UpdateBounds();
				if (AActor* Owner = MeshComponent->GetOwner())
				{
					if (ULevel* Level = Owner->GetLevel())
					{
						Level->MarkSpatialDirty();
					}
				}
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();

	if (!CurrentMesh)
	{
		ImGui::TextDisabled("No Static Mesh Assigned");
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("LOD");

	bool bLODEnabled = MeshComponent->IsLODEnabled();
	if (ImGui::Checkbox("Enable LOD", &bLODEnabled))
	{
		MeshComponent->SetLODEnabled(bLODEnabled);
	}

	const int32 CurrentLODIndex    = MeshComponent->GetCurrentLODIndex();
	const float CurrentLODDistance = MeshComponent->GetLastLODSelectionDistance();
	ImGui::Text("Current LOD");
	ImGui::SameLine(120.0f);
	if (CurrentLODIndex <= 0)
	{
		ImGui::Text("LOD0 (Base Mesh)");
	}
	else
	{
		ImGui::Text("LOD%d", CurrentLODIndex);
	}
	ImGui::Text("View Distance");
	ImGui::SameLine(120.0f);
	ImGui::Text("%.2f", CurrentLODDistance);

	const int32 LodDistanceCount = MeshComponent->GetLODDistanceCount();
	if (LodDistanceCount == 0)
	{
		ImGui::TextDisabled("No additional LOD files loaded.");
	}
	else
	{
		for (int32 LodIndex = 1; LodIndex <= LodDistanceCount; ++LodIndex)
		{
			float Distance = MeshComponent->GetLODDistance(LodIndex);
			char  Label[48];
			snprintf(Label, sizeof(Label), "LOD%d Start Distance", LodIndex);
			if (ImGui::DragFloat(Label, &Distance, 1.0f, 0.0f, 1000000.0f, "%.1f"))
			{
				MeshComponent->SetLODDistance(LodIndex, Distance);
			}
		}
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Materials");

	const TArray<std::filesystem::path> TexturePaths     = GetAvailableTexturePaths();
	FMaterialManager&                   MaterialManager  = FMaterialManager::Get();
	const uint32                        NumSections      = CurrentMesh->GetNumSections();
	TArray<FString>                     MaterialSources  = MaterialManager.GetMaterialSourceAssets();
	TArray<FString>                     AllMaterialNames = BuildMaterialPickerNames(MaterialManager.GetAllMaterialNames(), TexturePaths);

	std::sort(MaterialSources.begin(), MaterialSources.end());
	std::sort(AllMaterialNames.begin(), AllMaterialNames.end());

	FString& SelectedMaterialSource = MaterialSourceSelectionCache[MeshComponent];
	if (!SelectedMaterialSource.empty() &&
		std::find(MaterialSources.begin(), MaterialSources.end(), SelectedMaterialSource) == MaterialSources.end())
	{
		SelectedMaterialSource.clear();
	}

	const char* SourcePreview = SelectedMaterialSource.empty() ? "All Materials" : SelectedMaterialSource.c_str();
	if (ImGui::BeginCombo("Material Set (MTL)", SourcePreview))
	{
		const bool bAllSelected = SelectedMaterialSource.empty();
		if (ImGui::Selectable("All Materials", bAllSelected))
		{
			SelectedMaterialSource.clear();
		}
		if (bAllSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& SourceAsset : MaterialSources)
		{
			const bool bSelected = (SelectedMaterialSource == SourceAsset);
			if (ImGui::Selectable(SourceAsset.c_str(), bSelected))
			{
				SelectedMaterialSource = SourceAsset;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	const bool      bShowTextureMaterials = SelectedMaterialSource.empty();
	TArray<FString> MaterialNames         = bShowTextureMaterials
		                                ? AllMaterialNames
		                                : BuildMaterialPickerNames(MaterialManager.GetMaterialNamesBySource(SelectedMaterialSource), TexturePaths);
	std::sort(MaterialNames.begin(), MaterialNames.end());

	if (MaterialNames.empty() && !bShowTextureMaterials)
	{
		ImGui::TextDisabled("No materials available for selected set.");
	}

	const bool bHasSelectableMaterials = !MaterialNames.empty() || (bShowTextureMaterials && !TexturePaths.empty());
	ImGui::BeginDisabled(!bHasSelectableMaterials);
	if (ImGui::BeginCombo("Apply To All", "Select Material..."))
	{
		for (const FString& MaterialName : MaterialNames)
		{
			if (ImGui::Selectable(MaterialName.c_str(), false))
			{
				if (std::shared_ptr<FMaterial> Material = MaterialManager.FindByName(MaterialName))
				{
					for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
					{
						MeshComponent->SetMaterial(SectionIndex, Material);
					}
				}
			}
		}
		if (bShowTextureMaterials && !TexturePaths.empty())
		{
			ImGui::SeparatorText("Textures");
			for (const std::filesystem::path& TexturePath : TexturePaths)
			{
				const FString TextureMaterialName = GetTextureMaterialName(TexturePath);
				ImGui::PushID(TextureMaterialName.c_str());
				if (ImGui::Selectable(TextureMaterialName.c_str(), false))
				{
					if (std::shared_ptr<FMaterial> Material = LoadTextureMaterial(TexturePath))
					{
						for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
						{
							MeshComponent->SetMaterial(SectionIndex, Material);
						}
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	float MasterScroll[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (NumSections > 0)
	{
		if (std::shared_ptr<FMaterial> FirstMaterial = MeshComponent->GetMaterial(0))
		{
			FirstMaterial->GetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
		}
	}

	if (ImGui::DragFloat2("Scroll All Sections", MasterScroll, 0.001f, -5.0f, 5.0f, "%.2f"))
	{
		for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
		{
			if (std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(SectionIndex))
			{
				Material->SetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
			}
		}
	}

	for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		std::shared_ptr<FMaterial> CurrentMaterial     = MeshComponent->GetMaterial(SectionIndex);
		std::string                CurrentMaterialName = CurrentMaterial ? CurrentMaterial->GetOriginName() : "None";
		const std::string          ComboLabel          = "Section " + std::to_string(SectionIndex);

		ImGui::PushID(static_cast<int>(SectionIndex));
		ImGui::PushItemWidth(-1.0f);
		ImGui::BeginDisabled(!bHasSelectableMaterials);
		if (ImGui::BeginCombo(ComboLabel.c_str(), CurrentMaterialName.c_str()))
		{
			for (const FString& MaterialName : MaterialNames)
			{
				const bool bSelected = (CurrentMaterialName == MaterialName);
				if (ImGui::Selectable(MaterialName.c_str(), bSelected))
				{
					if (std::shared_ptr<FMaterial> Material = MaterialManager.FindByName(MaterialName))
					{
						MeshComponent->SetMaterial(SectionIndex, Material);
						CurrentMaterial     = Material;
						CurrentMaterialName = CurrentMaterial ? CurrentMaterial->GetOriginName() : "None";
					}
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			if (bShowTextureMaterials && !TexturePaths.empty())
			{
				ImGui::SeparatorText("Textures");
				for (const std::filesystem::path& TexturePath : TexturePaths)
				{
					const FString TextureMaterialName = GetTextureMaterialName(TexturePath);
					const bool    bSelected           = (CurrentMaterialName == TextureMaterialName);
					ImGui::PushID(TextureMaterialName.c_str());
					if (ImGui::Selectable(TextureMaterialName.c_str(), bSelected))
					{
						if (std::shared_ptr<FMaterial> Material = LoadTextureMaterial(TexturePath))
						{
							MeshComponent->SetMaterial(SectionIndex, Material);
							CurrentMaterial     = MeshComponent->GetMaterial(SectionIndex);
							CurrentMaterialName = CurrentMaterial ? CurrentMaterial->GetOriginName() : "None";
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
			}

			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::PopItemWidth();

		if (CurrentMaterial)
		{
			FLinearColor BaseColor(CurrentMaterial->GetVectorParameter("BaseColor"));
			if (EditLinearColor4("Base Color", BaseColor))
			{
				CurrentMaterial->SetLinearColorParameter("BaseColor", BaseColor);
			}

			float ScrollArray[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			CurrentMaterial->GetParameterData("UVScrollSpeed", ScrollArray, sizeof(ScrollArray));
			if (ImGui::DragFloat2("UV Scroll", ScrollArray, 0.001f, -5.0f, 5.0f, "%.2f"))
			{
				CurrentMaterial->SetParameterData("UVScrollSpeed", ScrollArray, sizeof(ScrollArray));
			}

			FLinearColor EmissiveColor(CurrentMaterial->GetVectorParameter("EmissiveColor"));
			if (EditLinearColor4("Emissive Color", EmissiveColor))
			{
				CurrentMaterial->SetLinearColorParameter("EmissiveColor", EmissiveColor);
			}

			float ShininessArray[4] = { 32.0f, 0.0f, 0.0f, 0.0f };
			CurrentMaterial->GetParameterData("Shininess", &ShininessArray, sizeof(ShininessArray));
			if (ImGui::DragFloat("Shininess", ShininessArray, 0.1f, 0.0f, 128.0f, "%.2f"))
			{
				CurrentMaterial->SetParameterData("Shininess", &ShininessArray, sizeof(ShininessArray));
			}

			DrawNormalTextureCombo(MeshComponent, static_cast<int32>(SectionIndex), CurrentMaterial);
		}
		ImGui::Spacing();
		ImGui::PopID();
	}

	DrawMaterialPreviewSection(MeshComponent, Engine);
}

void FPropertyWindow::DrawRotatingMovementComponentDetails(URotatingMovementComponent* RotatingMovementComponent)
{
	if (!RotatingMovementComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Rotating Movement");

	const FRotator RotationRate = RotatingMovementComponent->GetRotationRate();
	FVector        NewRotationRate(RotationRate.Pitch, RotationRate.Yaw, RotationRate.Roll);
	ImGui::Text("Rotation Rate");
	ImGui::NextColumn();
	if (DrawVector3Control("Rotation Rate (Pitch/Yaw/Roll)", NewRotationRate, NewRotationRate, 0.5f, "%.2f"))
	{
		RotatingMovementComponent->SetRotationRate(
			FRotator(NewRotationRate.X, NewRotationRate.Y, NewRotationRate.Z));
	}

	FVector NewPivotTranslation = RotatingMovementComponent->GetPivotTranslation();
	ImGui::Text("Pivot Translation");
	ImGui::NextColumn();
	if (DrawVector3Control("Pivot Translation", NewPivotTranslation, NewPivotTranslation, 0.5f, "%.2f"))
	{
		RotatingMovementComponent->SetPivotTranslation(NewPivotTranslation);
	}
}

void FPropertyWindow::DrawProjectileMovementComponentDetails(UProjectileMovementComponent* ProjectileMovementComponent, FEditorEngine* Engine)
{
	if (!ProjectileMovementComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Projectile Movement");

	FVector NewVelocity = ProjectileMovementComponent->GetVelocity();
	ImGui::Text("Velocity");
	ImGui::NextColumn();
	if (DrawVector3Control("Velocity", ProjectileMovementComponent->GetVelocity(), NewVelocity, 1.0f, "%.2f"))
	{
		ProjectileMovementComponent->SetVelocity(NewVelocity);
	}

	float GravityScale = ProjectileMovementComponent->GetGravityScale();
	ImGui::Text("Gravity Scale");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Gravity Scale", &GravityScale, 0.01f, -10.0f, 10.0f, "%.2f"))
	{
		ProjectileMovementComponent->SetGravityScale(GravityScale);
	}

	float MaxSpeed = ProjectileMovementComponent->GetMaxSpeed();
	ImGui::Text("Max Speed");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Max Speed", &MaxSpeed, 1.0f, 0.0f, 100000.0f, "%.2f"))
	{
		ProjectileMovementComponent->SetMaxSpeed(MaxSpeed);
	}

	bool bAutoStartSimulation = ProjectileMovementComponent->IsAutoStartSimulationEnabled();
	ImGui::Text("Auto Start Simulation");
	ImGui::NextColumn();
	if (ImGui::Checkbox("Auto Start Simulation", &bAutoStartSimulation))
	{
		ProjectileMovementComponent->SetAutoStartSimulation(bAutoStartSimulation);
		if (!bAutoStartSimulation)
		{
			ProjectileMovementComponent->StopSimulation();
		}
	}

	ImGui::TextDisabled(
		"Simulation: %s",
		ProjectileMovementComponent->IsSimulationEnabled() ? "Running" : "Stopped");

	if (!ProjectileMovementComponent->IsAutoStartSimulationEnabled())
	{
		const bool bIsPIEActive = Engine && Engine->IsPIEActive() && !Engine->IsPIEPaused();
		ImGui::BeginDisabled(!bIsPIEActive);
		if (ImGui::Button("Start Simulation In PIE"))
		{
			ProjectileMovementComponent->StartSimulation();
		}
		ImGui::EndDisabled();

		if (!bIsPIEActive)
		{
			ImGui::TextDisabled("Manual start is available while PIE is running.");
		}
	}
}

void FPropertyWindow::DrawSpringArmComponentDetails(USpringArmComponent* SpringArmComponent)
{
	if (!SpringArmComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Spring Arm");

	float TargetArmLength = SpringArmComponent->GetTargetArmLength();
	if (ImGui::DragFloat("Target Arm Length", &TargetArmLength, 1.0f, 0.0f, 10000.0f, "%.2f"))
	{
		SpringArmComponent->SetTargetArmLength(TargetArmLength);
	}

	FVector SocketOffset = SpringArmComponent->GetSocketOffset();
	ImGui::Text("Socket Offset");
	ImGui::NextColumn();
	if (DrawVector3Control("Socket Offset", SocketOffset, SocketOffset, 0.1f, "%.2f"))
	{
		SpringArmComponent->SetSocketOffset(SocketOffset);
	}
}


void FPropertyWindow::DrawTextComponentDetails(UTextRenderComponent* TextComponent)
{
	if (!TextComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Text");

	char TextBuffer[256] = {};
	snprintf(TextBuffer, sizeof(TextBuffer), "%s", TextComponent->GetText().c_str());
	if (ImGui::InputText("Text", TextBuffer, sizeof(TextBuffer)))
	{
		TextComponent->SetText(TextBuffer);
		TextComponent->UpdateBounds();
		if (AActor* Owner = TextComponent->GetOwner())
		{
			if (ULevel* Level = Owner->GetLevel())
			{
				Level->MarkSpatialDirty();
			}
		}
	}

	FLinearColor TextColor = TextComponent->GetTextColor();
	if (EditLinearColor4("Text Color", TextColor))
	{
		TextComponent->SetTextColorLinear(TextColor);
		TextComponent->MarkTextMeshDirty();
	}

	float TextScale = TextComponent->GetTextScale();
	if (ImGui::DragFloat("Text Scale", &TextScale, 0.01f, 0.01f, 100.0f, "%.2f"))
	{
		TextComponent->SetTextScale(TextScale);
		TextComponent->UpdateBounds();
		if (AActor* Owner = TextComponent->GetOwner())
		{
			if (ULevel* Level = Owner->GetLevel())
			{
				Level->MarkSpatialDirty();
			}
		}
	}

	bool bBillboard = TextComponent->IsBillboard();
	if (ImGui::Checkbox("Billboard", &bBillboard))
	{
		TextComponent->SetBillboard(bBillboard);
	}


	const char* HAlignOptions[] = { "Left", "Center", "Right" };
	int         HAlignIndex     = (int)TextComponent->GetHorizontalAlignment();
	if (ImGui::Combo("Horizontal Alignment", &HAlignIndex, HAlignOptions, IM_ARRAYSIZE(HAlignOptions)))
	{
		TextComponent->SetHorizontalAlignment((EHorizTextAligment)HAlignIndex);
	}

	const char* VAlignOptions[] = { "Top", "Center", "Bottom" };
	int         VAlignIndex     = (int)TextComponent->GetVerticalAlignment();
	if (ImGui::Combo("Vertical Alignment", &VAlignIndex, VAlignOptions, IM_ARRAYSIZE(VAlignOptions)))
	{
		TextComponent->SetVerticalAlignment((EVerticalTextAligment)VAlignIndex);
	}
}

void FPropertyWindow::DrawSubUVComponentDetails(USubUVComponent* SubUVComponent)
{
	if (!SubUVComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("SubUV");

	FVector2 Size         = SubUVComponent->GetSize();
	float    SizeArray[2] = { Size.X, Size.Y };
	if (ImGui::DragFloat2("Size", SizeArray, 0.01f, 0.01f, 100.0f, "%.2f"))
	{
		SubUVComponent->SetSize(FVector2(SizeArray[0], SizeArray[1]));
		SubUVComponent->UpdateBounds();
		if (AActor* Owner = SubUVComponent->GetOwner())
		{
			if (ULevel* Level = Owner->GetLevel())
			{
				Level->MarkSpatialDirty();
			}
		}
	}

	float FPS = SubUVComponent->GetFPS();
	if (ImGui::DragFloat("FPS", &FPS, 0.1f, 0.0f, 240.0f, "%.1f"))
	{
		SubUVComponent->SetFPS(FPS);
	}

	bool bLoop = SubUVComponent->IsLoop();
	if (ImGui::Checkbox("Loop", &bLoop))
	{
		SubUVComponent->SetLoop(bLoop);
	}

	bool bBillboard = SubUVComponent->IsBillboard();
	if (ImGui::Checkbox("Billboard", &bBillboard))
	{
		SubUVComponent->SetBillboard(bBillboard);
	}
}

void FPropertyWindow::DrawHeightFogComponentDetails(UHeightFogComponent* HeightFogComponent)
{
	if (!HeightFogComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Height Fog");

	if (ImGui::DragFloat("Fog Density", &HeightFogComponent->FogDensity, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		HeightFogComponent->FogDensity = (std::max)(0.0f, HeightFogComponent->FogDensity);
	}

	if (ImGui::DragFloat("Height Falloff", &HeightFogComponent->FogHeightFalloff, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		HeightFogComponent->FogHeightFalloff = (std::max)(0.0f, HeightFogComponent->FogHeightFalloff);
	}

	if (ImGui::DragFloat("Start Distance", &HeightFogComponent->StartDistance, 0.1f, 0.0f, 100000.0f, "%.2f"))
	{
		HeightFogComponent->StartDistance = (std::max)(0.0f, HeightFogComponent->StartDistance);
		if (HeightFogComponent->FogCutoffDistance > 0.0f)
		{
			HeightFogComponent->StartDistance = (std::min)(HeightFogComponent->StartDistance, HeightFogComponent->FogCutoffDistance);
		}
	}

	if (ImGui::DragFloat("Cutoff Distance", &HeightFogComponent->FogCutoffDistance, 0.1f, 0.0f, 100000.0f, "%.2f"))
	{
		HeightFogComponent->FogCutoffDistance = (std::max)(0.0f, HeightFogComponent->FogCutoffDistance);
		if (HeightFogComponent->FogCutoffDistance > 0.0f)
		{
			HeightFogComponent->FogCutoffDistance = (std::max)(HeightFogComponent->FogCutoffDistance, HeightFogComponent->StartDistance);
		}
	}

	ImGui::SliderFloat("Max Opacity", &HeightFogComponent->FogMaxOpacity, 0.0f, 1.0f, "%.2f");

	FLinearColor FogColor = HeightFogComponent->FogInscatteringColor;
	if (EditLinearColor4("Fog Color", FogColor))
	{
		HeightFogComponent->FogInscatteringColor = FogColor;
	}

	bool AllowBackground = (bool)HeightFogComponent->AllowBackground;
	if (ImGui::Checkbox("Allow Background", &AllowBackground))
	{
		HeightFogComponent->AllowBackground = (float)AllowBackground;
	}
}


void FPropertyWindow::DrawLocalHeightFogComponentDetails(ULocalHeightFogComponent* LocalHeightFogComponent)
{
	if (!LocalHeightFogComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Local Height Fog");
	ImGui::TextDisabled("Transform controls above affect local fog position / rotation. Debug bounds follow show flags even when not selected.");

	if (ImGui::DragFloat("Fog Density", &LocalHeightFogComponent->FogDensity, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		LocalHeightFogComponent->FogDensity = (std::max)(0.0f, LocalHeightFogComponent->FogDensity);
	}

	if (ImGui::DragFloat("Height Falloff", &LocalHeightFogComponent->FogHeightFalloff, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		LocalHeightFogComponent->FogHeightFalloff = (std::max)(0.0f, LocalHeightFogComponent->FogHeightFalloff);
	}


	ImGui::SliderFloat("Max Opacity", &LocalHeightFogComponent->FogMaxOpacity, 0.0f, 1.0f, "%.2f");

	FLinearColor FogColor = LocalHeightFogComponent->FogInscatteringColor;
	if (EditLinearColor4("Fog Color", FogColor))
	{
		LocalHeightFogComponent->FogInscatteringColor = FogColor;
	}

	bool AllowBackground = (bool)LocalHeightFogComponent->AllowBackground;
	if (ImGui::Checkbox("Allow Background", &AllowBackground))
	{
		LocalHeightFogComponent->AllowBackground = (float)AllowBackground;
	}

	float ExtentArray[3] =
	{
		LocalHeightFogComponent->FogExtents.X,
		LocalHeightFogComponent->FogExtents.Y,
		LocalHeightFogComponent->FogExtents.Z
	};
	if (ImGui::DragFloat3("Extents", ExtentArray, 1.0f, 1.0f, 100000.0f, "%.1f"))
	{
		LocalHeightFogComponent->FogExtents.X = (std::max)(1.0f, ExtentArray[0]);
		LocalHeightFogComponent->FogExtents.Y = (std::max)(1.0f, ExtentArray[1]);
		LocalHeightFogComponent->FogExtents.Z = (std::max)(1.0f, ExtentArray[2]);
		LocalHeightFogComponent->UpdateBounds();
		if (AActor* Owner = LocalHeightFogComponent->GetOwner())
		{
			if (ULevel* Level = Owner->GetLevel())
			{
				Level->MarkSpatialDirty();
			}
		}
	}
}

void FPropertyWindow::DrawBillboardComponentDetials(UBillboardComponent* BillboardComponent, FEditorEngine* Engine)
{
	std::wstring CurrentPath     = BillboardComponent->GetTexturePath();
	std::string  CurrentFileName = std::filesystem::path(CurrentPath).filename().string();
	if (ImGui::BeginCombo("Sprite", CurrentFileName.c_str()))
	{
		for (auto& Pair : Engine->GetRenderer()->GetBillboardRenderer().GetTextureCache())
		{
			std::string FileName  = std::filesystem::path(Pair.first).filename().string();
			bool        bSelected = (Pair.first == CurrentPath);
			if (ImGui::Selectable(FileName.c_str(), bSelected))
			{
				BillboardComponent->SetTexturePath(Pair.first);
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::Separator();

	float Size[2] = { BillboardComponent->GetSize().X, BillboardComponent->GetSize().Y };
	if (ImGui::DragFloat2("Size", Size, 0.01f, 0.01f, 100.f, "%.2f"))
	{
		BillboardComponent->SetSize(FVector2(Size[0], Size[1]));
	}

	FLinearColor BillboardBaseColor = BillboardComponent->GetBaseColor();
	if (EditLinearColor4("Base Color", BillboardBaseColor))
	{
		BillboardComponent->SetBaseColorLinear(BillboardBaseColor);
	}

	float U  = BillboardComponent->GetUVMin().X;
	float V  = BillboardComponent->GetUVMin().Y;
	float UL = BillboardComponent->GetUVMax().X;
	float VL = BillboardComponent->GetUVMax().Y;

	ImGui::Separator();

	ImGui::DragFloat("U", &U, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("V", &V, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("UL", &UL, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("VL", &VL, 0.01f, 0.0f, 1.0f);

	BillboardComponent->SetUVMin(FVector2(U, V));
	BillboardComponent->SetUVMax(FVector2(UL, VL));
}

void FPropertyWindow::DrawDecalComponentDetails(UDecalComponent* DecalComponent, FEditorEngine* Engine)
{
	bool bEnabled = DecalComponent->IsEnabled();
	if (ImGui::Checkbox("Enabled", &bEnabled))
	{
		DecalComponent->SetEnabled(bEnabled);
	}

	float Size[2] = { DecalComponent->GetSize().X, DecalComponent->GetSize().Y };
	if (ImGui::DragFloat2("Size", Size, 1.0f, 0.0f, 10000.0f, "%.2f"))
	{
		DecalComponent->SetSize(FVector2(Size[0], Size[1]));
	}

	float ProjectionDepth = DecalComponent->GetProjectionDepth();
	if (ImGui::DragFloat("Projection Depth", &ProjectionDepth, 1.0f, 0.0f, 10000.0f, "%.2f"))
	{
		DecalComponent->SetProjectionDepth(ProjectionDepth);
	}

	FVector2 UVMin = DecalComponent->GetUVMin();
	FVector2 UVMax = DecalComponent->GetUVMax();
	if (ImGui::DragFloat2("UV Min", &UVMin.X, 0.01f, 0.0f, 1.0f, "%.3f"))
	{
		DecalComponent->SetUVMin(UVMin);
	}
	if (ImGui::DragFloat2("UV Max", &UVMax.X, 0.01f, 0.0f, 1.0f, "%.3f"))
	{
		DecalComponent->SetUVMax(UVMax);
	}

	float AllowAngle = DecalComponent->GetAllowAngle();
	if (ImGui::DragFloat("Allow Angle", &AllowAngle, 1.0f, 0.0f, 180.0f, "%.1f"))
	{
		DecalComponent->SetAllowAngle(AllowAngle);
	}

	TArray<std::filesystem::path> TextureFiles;
	CollectTextureFiles(FPaths::ContentDir() / "Textures", TextureFiles);

	std::wstring CurrentPath = DecalComponent->GetTexturePath();
	if (AActor* OwnerActor = DecalComponent->GetOwner())
	{
		if (OwnerActor->IsA(ASpotLightFakeActor::StaticClass()) && CurrentPath == L"__SpotLightFakeCircularMask__")
		{
			CurrentPath.clear();
		}
	}
	std::string CurrentLabel = CurrentPath.empty()
		                           ? std::string("(None)")
		                           : std::filesystem::path(CurrentPath).filename().string();

	if (ImGui::BeginCombo("Decal Texture", CurrentLabel.c_str()))
	{
		bool bNoneSelected = CurrentPath.empty();
		if (ImGui::Selectable("(None)", bNoneSelected))
		{
			if (AActor* OwnerActor = DecalComponent->GetOwner())
			{
				if (OwnerActor->IsA(ASpotLightFakeActor::StaticClass()))
				{
					static_cast<ASpotLightFakeActor*>(OwnerActor)->SetDecalTexturePath(L"");
				}
				else
				{
					DecalComponent->SetTexturePath(L"");
				}
			}
			else
			{
				DecalComponent->SetTexturePath(L"");
			}
		}

		for (const auto& Path : TextureFiles)
		{
			const std::string  Label     = Path.filename().string();
			const std::wstring FullPath  = Path.wstring();
			const bool         bSelected = (FullPath == CurrentPath);
			const std::string  PathId    = Path.string();
			ImGui::PushID(PathId.c_str());
			if (ImGui::Selectable(Label.c_str(), bSelected))
			{
				if (AActor* OwnerActor = DecalComponent->GetOwner())
				{
					if (OwnerActor->IsA(ASpotLightFakeActor::StaticClass()))
					{
						static_cast<ASpotLightFakeActor*>(OwnerActor)->SetDecalTexturePath(FullPath);
					}
					else
					{
						DecalComponent->SetTexturePath(FullPath);
					}
				}
				else
				{
					DecalComponent->SetTexturePath(FullPath);
				}
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	FLinearColor Tint = DecalComponent->GetBaseColorTint();
	if (EditLinearColor4("Base Color Tint", Tint))
	{
		DecalComponent->SetBaseColorTint(Tint);
	}

	float EdgeFade = DecalComponent->GetEdgeFade();
	if (ImGui::DragFloat("Edge Fade", &EdgeFade, 0.05f, 0.0f, 32.0f, "%.2f"))
	{
		DecalComponent->SetEdgeFade(EdgeFade);
	}

	ImGui::Separator();
	ImGui::Text("Fade Preview");

	float FadeInDuration  = DecalComponent->GetFadeInDuration();
	float FadeOutDuration = DecalComponent->GetFadeOutDuration();

	if (ImGui::DragFloat("Fade In Duration", &FadeInDuration, 0.05f, 0.05f, 10.0f, "%.2f s"))
	{
		DecalComponent->SetFadeInDuration(FadeInDuration);
	}
	if (ImGui::DragFloat("Fade Out Duration", &FadeOutDuration, 0.05f, 0.05f, 10.0f, "%.2f s"))
	{
		DecalComponent->SetFadeOutDuration(FadeOutDuration);
	}

	if (ImGui::Button("Fade In"))
	{
		DecalComponent->FadeIn(FadeInDuration);
	}
	ImGui::SameLine();
	if (ImGui::Button("Fade Out"))
	{
		DecalComponent->FadeOut(FadeOutDuration, true);
	}

	EDecalFadeState FadeState      = DecalComponent->GetFadeState();
	auto            FadeStateLabel = "None";
	if (FadeState == EDecalFadeState::FadeIn)
	{
		FadeStateLabel = "Fading In";
	}
	if (FadeState == EDecalFadeState::FadeOut)
	{
		FadeStateLabel = "Fading Out";
	}
	ImGui::Text("State: %s", FadeStateLabel);

	float CurrentAlpha = DecalComponent->GetBaseColorTint().A;
	ImGui::ProgressBar(CurrentAlpha, ImVec2(-1.0f, 0.0f), "Alpha");
}

void FPropertyWindow::DrawMeshDecalComponentDetails(UMeshDecalComponent* MeshDecalComponent, FEditorEngine* Engine)
{
	if (!MeshDecalComponent)
	{
		return;
	}

	DrawDecalComponentDetails(MeshDecalComponent, Engine);

	float SurfaceOffset = MeshDecalComponent->GetSurfaceOffset();
	if (ImGui::DragFloat("Surface Offset", &SurfaceOffset, 0.0001f, 0.0f, 0.1f, "%.4f"))
	{
		MeshDecalComponent->SetSurfaceOffset(SurfaceOffset);
	}
}

void FPropertyWindow::DrawFireBallComponentDetails(UFireBallComponent* FireBallComponent)
{
	if (!FireBallComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("FireBall");

	float Intensity = FireBallComponent->GetIntensity();
	if (ImGui::DragFloat("Intensity", &Intensity, 0.01f, 0.0f, 100.0f, "%.2f"))
	{
		FireBallComponent->SetIntensity((std::max)(0.0f, Intensity));
	}

	float Radius = FireBallComponent->GetRadius();
	if (ImGui::DragFloat("Radius", &Radius, 0.1f, 0.0f, 10000.0f, "%.2f"))
	{
		FireBallComponent->SetRadius((std::max)(0.0f, Radius));
	}

	float RadiusFallOff = FireBallComponent->GetRadiusFallOff();
	if (ImGui::DragFloat("Radius FallOff", &RadiusFallOff, 0.01f, 0.0f, 100.0f, "%.2f"))
	{
		FireBallComponent->SetRadiusFallOff((std::max)(0.0f, RadiusFallOff));
	}

	FLinearColor Color = FireBallComponent->GetColor();
	if (EditLinearColor4("Color", Color))
	{
		FireBallComponent->SetColor(Color);
	}
}

void FPropertyWindow::DrawLightComponentDetails(ULightComponent* LightComponent, FEditorEngine* Engine)
{
	if (!LightComponent)
	{
		return;
	}

	RefreshAttachedLightBillboard(LightComponent);

	ImGui::Spacing();
	ImGui::TextDisabled("Light");

	bool bVisible = LightComponent->GetVisible();
	ImGui::Text("Visible");
	ImGui::NextColumn();
	if (ImGui::Checkbox("Light Visible", &bVisible))
	{
		LightComponent->SetVisible(bVisible);
	}

	bool bCastShadows = LightComponent->IsCastingShadows();
	ImGui::Text("Cast Shadows");
	ImGui::NextColumn();
	if (ImGui::Checkbox("bCastShadows", &bCastShadows))
	{
		LightComponent->SetCastingShadows(bCastShadows);
	}

	float Intensity = LightComponent->GetIntensity();
	ImGui::Text("Intensity");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Intensity", &Intensity, 1.0f, 0.0f, 1000000.0f, "%.3f"))
	{
		LightComponent->SetIntensity(Intensity);
	}

	float ShadowResolutionScale = LightComponent->GetShadowResolutionScale();
	ImGui::Text("Shadow Map Resolution");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Shadow Map Resolution", &ShadowResolutionScale, 1.0f, 0.0f, 10.0f, "%.3f"))
	{
		LightComponent->SetShadowResolutionScale(ShadowResolutionScale);
	}

	float ShadowBias = LightComponent->GetShadowBias();
	ImGui::Text("Shadow Bias");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Shadow Bias", &ShadowBias, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		LightComponent->SetShadowBias(ShadowBias);
	}

	float ShadowSlopeBias = LightComponent->GetShadowSlopeBias();
	ImGui::Text("Shadow Slope Bias");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Shadow Slope Bias", &ShadowSlopeBias, 0.001f, 0.0f, 10.0f, "%.3f"))
	{
		LightComponent->SetShadowSlopeBias(ShadowSlopeBias);
	}

	float ShadowSharpeness = LightComponent->GetShadowSharpen();
	ImGui::Text("Shadow Sharpeness");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Shadow Sharpeness", &ShadowSharpeness, 0.01f, 0.0f, 1.0f, "%.2f"))
	{
		LightComponent->SetShadowSharpen(ShadowSharpeness);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("Shadow Map Debug");

	FRenderer*            Renderer      = Engine ? Engine->GetRenderer() : nullptr;
	FShadowRenderFeature* ShadowFeature = Renderer ? Renderer->GetShadowFeature() : nullptr;

	if (!ShadowFeature)
	{
		ImGui::TextDisabled("Shadow feature unavailable.");
	}
	else
	{
		bool bIsDirectional = LightComponent->IsA(UDirectionalLightComponent::StaticClass());
		if (ShadowFeature->GetDebugViewMode() != EShadowDebugViewMode::None)
		{
			ShadowFeature->SetDebugDirectional(bIsDirectional);
		}

		int DebugMode  = static_cast<int>(ShadowFeature->GetDebugViewMode());
		int DebugSlice = static_cast<int>(ShadowFeature->GetDebugViewSlice());

		const char* DebugModeNames[] =
		{
			"None",
			"Depth",
			"VSM Mean",
			"VSM Variance"
		};

		ImGui::Text("Debug View");
		ImGui::NextColumn();
		if (ImGui::Combo("Shadow Debug View", &DebugMode, DebugModeNames, IM_ARRAYSIZE(DebugModeNames)))
		{
			const EShadowDebugViewMode NewDebugMode = static_cast<EShadowDebugViewMode>(DebugMode);
			ShadowFeature->SetDebugViewMode(NewDebugMode);

			if (NewDebugMode != EShadowDebugViewMode::None &&
			    ShadowFeature->IsDebugViewportOverlayEnabled())
			{
				LightDebugPinnedLightComponent = LightComponent;
				LightDebugPinnedActor = LightComponent ? LightComponent->GetOwner() : nullptr;
				SelectedComponent = LightComponent;
			}
			else
			{
				ClearLightDebugPin();
			}
		}


		bool bShowInEditorViewport = ShadowFeature->IsDebugViewportOverlayEnabled();

		ImGui::Text("Editor Viewport");
		ImGui::NextColumn();
		ImGui::BeginDisabled(ShadowFeature->GetDebugViewMode() == EShadowDebugViewMode::None);
		if (ImGui::Checkbox("Show Shadow Debug In Editor Viewport", &bShowInEditorViewport))
		{
			ShadowFeature->SetDebugViewportOverlayEnabled(bShowInEditorViewport);
			if (bShowInEditorViewport &&
			    ShadowFeature->GetDebugViewMode() != EShadowDebugViewMode::None)
			{
				LightDebugPinnedLightComponent = LightComponent;
				LightDebugPinnedActor = LightComponent ? LightComponent->GetOwner() : nullptr;
				SelectedComponent = LightComponent;
			}
			else
			{
				ClearLightDebugPin();
			}
		}
		ImGui::EndDisabled();


		ImGui::Text("Debug Slice");
		ImGui::NextColumn();

		bIsDirectional = SelectedComponent && SelectedComponent->IsA(UDirectionalLightComponent::StaticClass());
		bool bIsSpot = SelectedComponent && SelectedComponent->IsA(USpotLightComponent::StaticClass());
		bool bIsPoint = SelectedComponent && SelectedComponent->IsA(UPointLightComponent::StaticClass());
		AActor* SelectedOwner = SelectedComponent ? SelectedComponent->GetOwner() : nullptr;

		const auto& AllViews = bIsDirectional ? ShadowFeature->GetLastDirectionalShadowViews() : ShadowFeature->GetLastLocalShadowViews();
		std::vector<const FShadowViewRenderItem*> MyViews;

		for (const auto& View : AllViews)
		{
			if (bIsDirectional || View.SourceActor == SelectedOwner)
			{
				MyViews.push_back(&View);
			}
		}

		const FShadowViewRenderItem* SelectedView = nullptr;

		if (MyViews.empty())
		{
			ImGui::TextDisabled("No shadow views found.");
		}
		else
		{
			if (bIsSpot)
			{
				ImGui::TextDisabled("Single Slice (Fixed)");
				SelectedView = MyViews[0];
				ShadowFeature->SetDebugViewSlice(SelectedView->ArraySlice);
			}
			else
			{
				uint32 CurrentSlice = ShadowFeature->GetDebugViewSlice();

				bool bValid = false;
				for (auto* V : MyViews) { if (V->ArraySlice == CurrentSlice) { bValid = true; break; } }
				if (!bValid)
				{
					CurrentSlice = MyViews[0]->ArraySlice;
					ShadowFeature->SetDebugViewSlice(CurrentSlice);
				}

				char CurrentLabel[64];
				snprintf(CurrentLabel, sizeof(CurrentLabel), bIsDirectional ? "Cascade %u" : "Face %u", CurrentSlice);

				if (ImGui::BeginCombo("##ShadowDebugSlice", CurrentLabel))
				{
					for (size_t i = 0; i < MyViews.size(); ++i)
					{
						uint32 SliceVal = MyViews[i]->ArraySlice;
						char Label[64];
						snprintf(Label, sizeof(Label), bIsDirectional ? "Cascade %zu" : "Face %zu", i);

						bool bSelected = (SliceVal == CurrentSlice);
						if (ImGui::Selectable(Label, bSelected))
						{
							ShadowFeature->SetDebugViewSlice(SliceVal);
						}
						if (bSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				for (auto* V : MyViews) { if (V->ArraySlice == ShadowFeature->GetDebugViewSlice()) { SelectedView = V; break; } }
			}
		}

		if (SelectedView && ShadowFeature->GetDebugViewMode() != EShadowDebugViewMode::None)
		{
			const float PreviewWidth = (std::min)(ImGui::GetContentRegionAvail().x, 256.0f);
			const ImVec2 PreviewSize(PreviewWidth, PreviewWidth);

			ImGui::Spacing();

			if (SelectedView->bAtlasAllocated)
			{
				ID3D11ShaderResourceView* AtlasSRV = nullptr;
				if (ShadowFeature->GetDebugViewMode() == EShadowDebugViewMode::Depth)
				{
					AtlasSRV = bIsDirectional ? ShadowFeature->GetDirShadowDepthAtlasSRV() : ShadowFeature->GetLocalShadowAtlasPreviewSRV();
				}
				else
				{
					AtlasSRV = bIsDirectional ? ShadowFeature->GetDirShadowMomentsAtlasSRV() : ShadowFeature->GetLocalShadowMomentsAtlasSRV();
				}

				if (AtlasSRV)
				{
					const float AtlasRes = static_cast<float>(bIsDirectional ? ShadowConfig::DirMaxShadowDepthResolution : ShadowConfig::MaxShadowMapResolution);

					ImVec2 uv0(SelectedView->AtlasUV.X / AtlasRes, SelectedView->AtlasUV.Y / AtlasRes);
					ImVec2 uv1((SelectedView->AtlasUV.X + SelectedView->AtlasUV.Z) / AtlasRes,
						(SelectedView->AtlasUV.Y + SelectedView->AtlasUV.Z) / AtlasRes);

					ImGui::TextDisabled(bIsDirectional ? "Preview: Cascade Map" : "Preview: Local Map");
					ImGui::Image(reinterpret_cast<ImTextureID>(AtlasSRV), PreviewSize, uv0, uv1);
				}
			}
			else
			{
				ImGui::TextDisabled("Shadow Atlas not allocated yet.");
			}
		}
		else
		{
			ImGui::TextDisabled("Preview unavailable.");
		}
	}

	const ELightUnits CurrentUnit      = LightComponent->GetIntensityUnits();
	const char*       CurrentUnitLabel = GetLightUnitLabel(CurrentUnit);
	ImGui::Text("Units");
	ImGui::NextColumn();
	ImGui::PushItemWidth(-1.0f);
	if (ImGui::BeginCombo("Intensity Units", CurrentUnitLabel))
	{
		for (ELightUnits Candidate : GLightUnitOrder)
		{
			if (!LightComponent->SupportsIntensityUnit(Candidate))
			{
				continue;
			}

			const bool bSelected = (CurrentUnit == Candidate);
			if (ImGui::Selectable(GetLightUnitLabel(Candidate), bSelected))
			{
				LightComponent->SetIntensityUnits(Candidate);
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();

	FLinearColor LightColor = LightComponent->GetColor();
	ImGui::Text("Color");
	ImGui::NextColumn();
	if (EditLinearColor3("Light Color", LightColor, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_Float))
	{
		LightComponent->SetColor(LightColor);
	}
}

void FPropertyWindow::DrawPointLightComponentDetails(UPointLightComponent* PointLightComponent, bool bShowHeader)
{
	if (!PointLightComponent)
	{
		return;
	}

	if (bShowHeader)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Point Light");
	}

	float AttenuationRadius = PointLightComponent->GetAttenuationRadius();
	ImGui::Text("Attenuation Radius");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Attenuation Radius", &AttenuationRadius, 0.1f, 0.0f, 100000.0f, "%.3f"))
	{
		PointLightComponent->SetAttenuationRadius(AttenuationRadius);
	}

	float FalloffExponent = PointLightComponent->GetLightFalloffExponent();
	ImGui::Text("Falloff Exponent");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Falloff Exponent", &FalloffExponent, 0.01f, 0.0f, 64.0f, "%.3f"))
	{
		PointLightComponent->SetLightFalloffExponent(FalloffExponent);
	}

	RefreshAttachedPointLightGizmo(PointLightComponent);
}

void FPropertyWindow::DrawSpotLightComponentDetails(USpotLightComponent* SpotLightComponent)
{
	if (!SpotLightComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Spot Light");

	float InnerCone = SpotLightComponent->GetInnerConeAngle();
	float OuterCone = SpotLightComponent->GetOuterConeAngle();

	ImGui::Text("Inner Cone Angle");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Inner Cone Angle", &InnerCone, 0.1f, 0.0f, 179.0f, "%.2f"))
	{
		InnerCone = FMath::Clamp(InnerCone, 0.0f, 179.0f);
		if (InnerCone > OuterCone)
		{
			OuterCone = InnerCone;
		}
		SpotLightComponent->SetInnerConeAngle(InnerCone);
		SpotLightComponent->SetOuterConeAngle(OuterCone);
	}

	ImGui::Text("Outer Cone Angle");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Outer Cone Angle", &OuterCone, 0.1f, 0.0f, 179.0f, "%.2f"))
	{
		OuterCone = FMath::Clamp(OuterCone, 0.0f, 179.0f);
		if (OuterCone < InnerCone)
		{
			InnerCone = OuterCone;
		}
		SpotLightComponent->SetOuterConeAngle(OuterCone);
		SpotLightComponent->SetInnerConeAngle(InnerCone);
	}

	RefreshAttachedSpotLightGizmo(SpotLightComponent);
}

void FPropertyWindow::DrawDirectionalLightComponentDetails(class UDirectionalLightComponent* DirectionalLightComponent)
{
	if (!DirectionalLightComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Directional Light (CSM)");

	int cascadeCount = DirectionalLightComponent->GetCascadeCount();
	ImGui::Text("Cascade Count");
	ImGui::NextColumn();
	if (ImGui::SliderInt("Cascade Count", &cascadeCount, 1, 4))
	{
		DirectionalLightComponent->SetCascadeCount(cascadeCount);
	}

	float shadowFarZ = DirectionalLightComponent->GetShadowFarZ();
	ImGui::Text("Shadow Far Z");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Shadow Far Z", &shadowFarZ, 1.0f, 0.0f, 2000.0f, "%.1f"))
	{
		DirectionalLightComponent->SetShadowFarZ(shadowFarZ);
	}

	float splitLambda = DirectionalLightComponent->GetSplitLambda();
	ImGui::Text("Split Lambda");
	ImGui::NextColumn();
	if (ImGui::SliderFloat("Split Lambda", &splitLambda, 0.0f, 1.0f, "%.3f"))
	{
		DirectionalLightComponent->SetSplitLambda(splitLambda);
	}

	ImGui::Separator();
	ImGui::TextDisabled("Cascade Biases");

	for (int i = 0; i < cascadeCount; ++i)
	{
		ImGui::PushID(i);
		ImGui::Text("Cascade %d", i);
		ImGui::NextColumn();

		float bias = DirectionalLightComponent->GetCascadeBias(i);
		if (ImGui::DragFloat("Bias", &bias, 0.0001f, 0.0f, 1.0f, "%.5f"))
			DirectionalLightComponent->SetCascadeBias(i, bias);

		float slopeBias = DirectionalLightComponent->GetCascadeSlopeBias(i);
		if (ImGui::DragFloat("Slope Bias", &slopeBias, 0.001f, 0.0f, 10.0f, "%.4f"))
			DirectionalLightComponent->SetCascadeSlopeBias(i, slopeBias);

		ImGui::Separator();
		ImGui::PopID();
	}

	float cascadeTransition = DirectionalLightComponent->GetCascadeTransitionValue();
	ImGui::Text("Cascade Transition");
	ImGui::NextColumn();
	if (ImGui::DragFloat("Cascade Transition", &cascadeTransition, 0.001f, 0.0f, 1.0f, "%.3f"))
	{
		DirectionalLightComponent->SetCascadeTransitionValue(cascadeTransition);
	}
}

void FPropertyWindow::DrawMaterialPreviewSection(UStaticMeshComponent* MeshComponent, FEditorEngine* Engine)
{
	if (!MeshComponent || !Engine)
	{
		return;
	}

	UStaticMesh* CurrentMesh = MeshComponent->GetStaticMesh();
	if (!CurrentMesh)
	{
		return;
	}

	const int32 NumSections = static_cast<int32>(CurrentMesh->GetNumSections());
	if (NumSections <= 0)
	{
		return;
	}

	if (PreviewedMeshComponent != MeshComponent)
	{
		PreviewedMeshComponent      = MeshComponent;
		PreviewMaterialSectionIndex = 0;
	}

	PreviewMaterialSectionIndex = FMath::Clamp(PreviewMaterialSectionIndex, 0, NumSections - 1);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextDisabled("Material Preview");

	if (NumSections > 1)
	{
		const std::shared_ptr<FMaterial> CurrentPreviewMaterial = MeshComponent->GetMaterial(PreviewMaterialSectionIndex);
		const std::string                CurrentSectionLabel    =
				"Section " + std::to_string(PreviewMaterialSectionIndex) + " - "
				+ (CurrentPreviewMaterial ? CurrentPreviewMaterial->GetOriginName() : std::string("None"));

		ImGui::PushItemWidth(-1.0f);
		if (ImGui::BeginCombo("Preview Section", CurrentSectionLabel.c_str()))
		{
			for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
			{
				const std::shared_ptr<FMaterial> SectionMaterial = MeshComponent->GetMaterial(SectionIndex);
				const std::string                Label           =
						"Section " + std::to_string(SectionIndex) + " - "
						+ (SectionMaterial ? SectionMaterial->GetOriginName() : std::string("None"));
				const bool bSelected = (PreviewMaterialSectionIndex == SectionIndex);
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					PreviewMaterialSectionIndex = SectionIndex;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
	}

	std::shared_ptr<FMaterial> PreviewMaterial = MeshComponent->GetMaterial(PreviewMaterialSectionIndex);
	if (!PreviewMaterial)
	{
		ImGui::TextDisabled("No material assigned to this section.");
		return;
	}

	const float  PreviewWidth  = (std::max)(ImGui::GetContentRegionAvail().x, 160.0f);
	const float  PreviewHeight = FMath::Clamp(PreviewWidth * 0.72f, 160.0f, 260.0f);
	const ImVec2 PreviewSize(PreviewWidth, PreviewHeight);

	if (RenderMaterialPreview(MeshComponent, PreviewMaterial.get(), Engine, PreviewSize)
		&& MaterialPreviewViewport && MaterialPreviewViewport->GetSRV())
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(MaterialPreviewViewport->GetSRV()), PreviewSize);

		if (ImGui::IsItemHovered())
		{
			const ImGuiIO& IO = ImGui::GetIO();
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				PreviewOrbitYaw   += IO.MouseDelta.x * 0.35f;
				PreviewOrbitPitch = FMath::Clamp(PreviewOrbitPitch - IO.MouseDelta.y * 0.35f, -80.0f, 80.0f);
			}

			if (IO.MouseWheel != 0.0f)
			{
				PreviewOrbitDistance = FMath::Clamp(PreviewOrbitDistance - IO.MouseWheel * 0.25f, 1.5f, 8.0f);
			}
		}
	}
	else
	{
		ImGui::BeginChild("##MaterialPreviewFallback", PreviewSize, true);
		ImGui::TextDisabled("Material preview unavailable.");
		ImGui::EndChild();
	}

	ImGui::TextDisabled("Drag to orbit, mouse wheel to zoom.");
}

bool FPropertyWindow::RenderMaterialPreview(
	UStaticMeshComponent* MeshComponent,
	FMaterial*            PreviewMaterial,
	FEditorEngine*        Engine,
	const ImVec2&         PreviewSize)
{
	if (!MeshComponent || !PreviewMaterial || !Engine)
	{
		return false;
	}

	FRenderer* Renderer = Engine->GetRenderer();
	if (!Renderer)
	{
		return false;
	}

	struct FRestoreMainRenderTargetScope
	{
		FRenderer* Renderer = nullptr;

		~FRestoreMainRenderTargetScope()
		{
			if (Renderer)
			{
				Renderer->GetRenderDevice().BindSwapChainRTV();
			}
		}
	} RestoreScope { Renderer };

	if (!EnsureMaterialPreviewScene(Engine))
	{
		return false;
	}

	if (!PreviewSphereMesh)
	{
		PreviewSphereMesh = FObjManager::LoadModelStaticMeshAsset(FPaths::FromPath(FPaths::MeshDir() / "PrimitiveSphere.Model"));
	}

	if (!PreviewSphereMesh || !PreviewSphereMesh->GetRenderData())
	{
		return false;
	}

	if (!MaterialPreviewViewport)
	{
		MaterialPreviewViewport = std::make_unique<FViewport>();
	}

	if (!MaterialPreviewTargetManager)
	{
		MaterialPreviewTargetManager = std::make_unique<FSceneTargetManager>();
	}

	const int32 PreviewWidth  = (std::max)(static_cast<int32>(PreviewSize.x), 1);
	const int32 PreviewHeight = (std::max)(static_cast<int32>(PreviewSize.y), 1);
	MaterialPreviewViewport->SetRect({ 0, 0, PreviewWidth, PreviewHeight });
	MaterialPreviewViewport->EnsureResources(Renderer->GetDevice());
	if (!MaterialPreviewViewport->GetRTV() || !MaterialPreviewViewport->GetDSV())
	{
		return false;
	}

	FWorldContext* PreviewContext = Engine->CreatePreviewWorldContext(GMaterialPreviewContextName, PreviewWidth, PreviewHeight);
	if (!PreviewContext || !PreviewContext->World)
	{
		return false;
	}

	UWorld*           PreviewWorld      = PreviewContext->World;
	UCameraComponent* PreviewCamera     = PreviewWorld->GetActiveCameraComponent();
	FCamera*          PreviewCameraData = PreviewCamera ? PreviewCamera->GetCamera() : nullptr;
	if (!PreviewCamera || !PreviewCameraData)
	{
		return false;
	}

	const float AspectRatio = static_cast<float>(PreviewWidth) / static_cast<float>(PreviewHeight);
	PreviewCameraData->SetAspectRatio(AspectRatio);
	PreviewCamera->SetFov(40.0f);
	PreviewCameraData->SetRotation(PreviewOrbitYaw, PreviewOrbitPitch);
	PreviewCameraData->SetPosition(FVector::ZeroVector - PreviewCameraData->GetForward() * PreviewOrbitDistance);

	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX       = 0.0f;
	Viewport.TopLeftY       = 0.0f;
	Viewport.Width          = static_cast<float>(PreviewWidth);
	Viewport.Height         = static_cast<float>(PreviewHeight);
	Viewport.MinDepth       = 0.0f;
	Viewport.MaxDepth       = 1.0f;

	FSceneViewRenderRequest SceneView;
	SceneView.ViewMatrix       = PreviewCamera->GetViewMatrix();
	SceneView.ProjectionMatrix = PreviewCamera->GetProjectionMatrix();
	SceneView.CameraPosition   = PreviewCameraData->GetPosition();
	SceneView.NearZ            = PreviewCamera->GetNearPlane();
	SceneView.FarZ             = PreviewCamera->GetFarPlane();
	SceneView.TotalTimeSeconds = static_cast<float>(Engine->GetTimer().GetTotalTime());

	FFrustum Frustum;
	Frustum.ExtractFromVP(SceneView.ViewMatrix * SceneView.ProjectionMatrix);

	FSceneRenderPacket ScenePacket;
	if (ULevel* PreviewLevel = PreviewWorld->GetPersistentLevel())
	{
		TArray<UPrimitiveComponent*> VisiblePrimitives;
		PreviewLevel->QueryPrimitivesByFrustum(Frustum, VisiblePrimitives);

		FScenePacketBuilder ScenePacketBuilder;
		const FShowFlags    PreviewShowFlags = BuildMaterialPreviewShowFlags();
		ScenePacketBuilder.BuildScenePacket(VisiblePrimitives, PreviewShowFlags, ScenePacket);
		ScenePacket.bApplyFXAA = PreviewShowFlags.HasFlag(EEngineShowFlags::SF_FXAA);
	}

	FMeshBatch PreviewBatch;
	PreviewBatch.Mesh        = PreviewSphereMesh->GetRenderData();
	PreviewBatch.Material    = PreviewMaterial;
	PreviewBatch.World       = FMatrix::Identity;
	PreviewBatch.WorldBounds = PreviewSphereMesh->LocalBounds;
	PreviewBatch.Domain      = EMaterialDomain::Opaque;
	PreviewBatch.PassMask    =
			static_cast<uint32>(EMeshPassMask::DepthPrepass) |
			static_cast<uint32>(EMeshPassMask::GBuffer) |
			static_cast<uint32>(EMeshPassMask::ForwardOpaque);

	if (PreviewBatch.Mesh->GetNumSection() > 0)
	{
		const FMeshSection& Section = PreviewBatch.Mesh->Sections[0];
		PreviewBatch.SectionIndex   = 0;
		PreviewBatch.IndexStart     = Section.StartIndex;
		PreviewBatch.IndexCount     = Section.IndexCount;
	}

	TArray<FMeshBatch> AdditionalMeshBatches;
	AdditionalMeshBatches.push_back(PreviewBatch);

	FSceneRenderTargets Targets;
	if (!MaterialPreviewTargetManager->WrapExternalSceneTargets(
		Renderer->GetDevice(),
		MaterialPreviewViewport->GetRTV(),
		MaterialPreviewViewport->GetSRV(),
		MaterialPreviewViewport->GetDSV(),
		MaterialPreviewViewport->GetDepthSRV(),
		Viewport,
		Targets))
	{
		return false;
	}

	const FFrameContext Frame = BuildRenderFrameContext(SceneView.TotalTimeSeconds);
	const FViewContext  View  = BuildRenderViewContext(SceneView, Viewport);

	FSceneViewData SceneViewData;
	SceneViewData.RenderMode = ERenderMode::Lit_Phong;
	Renderer->GetSceneRenderer().BuildSceneViewData(
		*Renderer,
		ScenePacket,
		Frame,
		View,
		PreviewWorld,
		AdditionalMeshBatches,
		SceneViewData);
	SceneViewData.ShowFlags         = BuildMaterialPreviewShowFlags();
	SceneViewData.DebugInputs.World = PreviewWorld;

	const float ClearColor[4] = { 0.09f, 0.09f, 0.10f, 1.0f };
	return Renderer->GetSceneRenderer().RenderSceneView(*Renderer, Targets, SceneViewData, ClearColor, false, nullptr);
}

bool FPropertyWindow::EnsureMaterialPreviewScene(FEditorEngine* Engine)
{
	if (!Engine)
	{
		return false;
	}

	FWorldContext* PreviewContext = Engine->CreatePreviewWorldContext(GMaterialPreviewContextName, 512, 512);
	if (!PreviewContext || !PreviewContext->World)
	{
		return false;
	}

	UWorld* PreviewWorld = PreviewContext->World;
	if (PreviewWorld->GetActors().empty())
	{
		AActor* AmbientActor = PreviewWorld->SpawnActor<AActor>(GMaterialPreviewAmbientActorName);
		if (AmbientActor)
		{
			UAmbientLightComponent* AmbientComponent =
					FObjectFactory::ConstructObject<UAmbientLightComponent>(AmbientActor, "MaterialPreviewAmbientComponent");
			EnsureActorComponentRegistered(AmbientActor, AmbientComponent);
			if (AmbientComponent)
			{
				AmbientComponent->SetIntensity(0.35f);
			}
		}

		AActor* DirectionalActor = PreviewWorld->SpawnActor<AActor>(GMaterialPreviewDirectionalActorName);
		if (DirectionalActor)
		{
			UDirectionalLightComponent* DirectionalComponent =
					FObjectFactory::ConstructObject<UDirectionalLightComponent>(DirectionalActor, "MaterialPreviewDirectionalComponent");
			EnsureActorComponentRegistered(DirectionalActor, DirectionalComponent);
			if (DirectionalComponent)
			{
				DirectionalComponent->SetIntensity(2.5f);
			}

			if (USceneComponent* RootComponent = DirectionalActor->GetRootComponent())
			{
				RootComponent->SetRelativeTransform(FTransform(FRotator(-35.0f, 45.0f, 0.0f), FVector::ZeroVector));
			}
		}
	}

	return PreviewWorld->GetActiveCameraComponent() != nullptr;
}

bool FPropertyWindow::DrawVector3Control(const char* Label, const FVector& Value, FVector& OutValue, float Speed, const char* Format)
{
	float Values[3] = { Value.X, Value.Y, Value.Z };
	ImGui::PushItemWidth(-1.0f);
	const bool bChanged = ImGui::DragFloat3(Label, Values, Speed, 0.0f, 0.0f, Format);
	ImGui::PopItemWidth();
	if (bChanged)
	{
		OutValue = FVector(Values[0], Values[1], Values[2]);
	}
	return bChanged;
}

bool FPropertyWindow::DrawAddComponentButton(AActor* SelectedActor)
{
	if (!SelectedActor)
	{
		return false;
	}

	bool             bAddedComponent        = false;
	constexpr float  AddButtonWidth         = 90.0f;
	USceneComponent* SelectedSceneComponent = GetSelectedSceneComponent(SelectedActor);

	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x > AddButtonWidth
		                     ? ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - AddButtonWidth
		                     : ImGui::GetCursorPosX());

	if (ImGui::Button("+ Add", ImVec2(AddButtonWidth, 0.0f)))
	{
		ImGui::OpenPopup("##AddComponentPopup");
	}

	if (ImGui::BeginPopup("##AddComponentPopup"))
	{
		ImGui::TextDisabled("Add Component");
		if (SelectedComponent && IsComponentOwnedByActor(SelectedActor, SelectedComponent))
		{
			const FString ComponentName = SelectedComponent->GetName().empty()
				                              ? SelectedComponent->GetClass()->GetName()
				                              : SelectedComponent->GetName();
			ImGui::Text("Target: %s", ComponentName.c_str());
		}
		else
		{
			ImGui::TextDisabled("Target: Select a component below");
		}
		ImGui::Separator();

		for (const FComponentAddOption& Option : GComponentAddOptions)
		{
			UClass*    OptionClass    = Option.GetClass();
			const bool bIsSceneOption = OptionClass && OptionClass->IsChildOf(USceneComponent::StaticClass());
			const bool bCanAdd        = OptionClass && (!bIsSceneOption || SelectedSceneComponent || !SelectedActor->GetRootComponent());

			ImGui::BeginDisabled(!bCanAdd);
			if (ImGui::Selectable(Option.Label))
			{
				bAddedComponent = AddComponentToActor(SelectedActor, OptionClass, Option.BaseName);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
		}

		ImGui::EndPopup();
	}

	return bAddedComponent;
}

bool FPropertyWindow::AddComponentToActor(AActor* SelectedActor, UClass* ComponentClass, const char* BaseName)
{
	if (!SelectedActor || !ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return false;
	}

	USceneComponent* SelectedSceneComponent = GetSelectedSceneComponent(SelectedActor);
	const bool       bIsSceneComponentClass = ComponentClass->IsChildOf(USceneComponent::StaticClass());
	if (bIsSceneComponentClass && !SelectedSceneComponent && SelectedActor->GetRootComponent())
	{
		return false;
	}

	const FString ComponentName = BuildUniqueComponentName(
		SelectedActor,
		(BaseName && BaseName[0] != '\0') ? FString(BaseName) : ComponentClass->GetName());

	auto NewComponent = static_cast<UActorComponent*>(
		FObjectFactory::ConstructObject(ComponentClass, SelectedActor, ComponentName));
	if (!NewComponent)
	{
		return false;
	}

	NewComponent->SetInstanceComponent(true);
	SelectedActor->AddOwnedComponent(NewComponent);

	if (NewComponent->IsA(USceneComponent::StaticClass()))
	{
		auto NewSceneComponent = static_cast<USceneComponent*>(NewComponent);
		if (SelectedSceneComponent)
		{
			NewSceneComponent->AttachTo(SelectedSceneComponent);
		}
		else
		{
			SelectedActor->SetRootComponent(NewSceneComponent);
		}
	}
	else if (NewComponent->IsA(UMovementComponent::StaticClass()) && SelectedSceneComponent)
	{
		static_cast<UMovementComponent*>(NewComponent)->SetUpdatedComponent(SelectedSceneComponent);
	}

	if (!NewComponent->IsRegistered())
	{
		NewComponent->OnRegister();
	}

	if (NewComponent->IsA(UPrimitiveComponent::StaticClass()))
	{
		auto PrimitiveComponent = static_cast<UPrimitiveComponent*>(NewComponent);
		PrimitiveComponent->UpdateBounds();
	}

	if (NewComponent->IsA(UTextRenderComponent::StaticClass()))
	{
		auto TextComponent = static_cast<UTextRenderComponent*>(NewComponent);
		TextComponent->MarkTextMeshDirty();
	}

	if (NewComponent->IsA(UDecalComponent::StaticClass()))
	{
		auto DecalComponent = static_cast<UDecalComponent*>(NewComponent);
		DecalComponent->UpdateBounds();
	}

	if (NewComponent->IsA(UMeshDecalComponent::StaticClass()))
	{
		auto MeshDecalComponent = static_cast<UMeshDecalComponent*>(NewComponent);
		MeshDecalComponent->UpdateBounds();
	}

	EnsureLightDebugAttachments(NewComponent);

	if (ULevel* Level = SelectedActor->GetLevel())
	{
		Level->MarkSpatialDirty();
	}

	if (SelectedActor->HasBegunPlay())
	{
		NewComponent->BeginPlay();
	}

	SelectedComponent = NewComponent;
	return true;
}

bool FPropertyWindow::IsComponentOwnedByActor(AActor* SelectedActor, UActorComponent* Component) const
{
	if (!SelectedActor || !Component)
	{
		return false;
	}

	for (UActorComponent* OwnedComponent : SelectedActor->GetComponents())
	{
		if (OwnedComponent == Component && !IsHiddenEditorOnlyComponent(OwnedComponent))
		{
			return true;
		}
	}

	return false;
}

USceneComponent* FPropertyWindow::GetSelectedSceneComponent(AActor* SelectedActor) const
{
	if (!IsComponentOwnedByActor(SelectedActor, SelectedComponent))
	{
		return nullptr;
	}

	if (!SelectedComponent->IsA(USceneComponent::StaticClass()))
	{
		if (SelectedComponent->IsA(UMovementComponent::StaticClass()))
		{
			USceneComponent* UpdatedComponent = static_cast<UMovementComponent*>(SelectedComponent)->GetUpdatedComponent();
			return IsComponentOwnedByActor(SelectedActor, UpdatedComponent) ? UpdatedComponent : nullptr;
		}

		return nullptr;
	}

	return static_cast<USceneComponent*>(SelectedComponent);
}

void FPropertyWindow::ClearLightDebugPin()
{
	LightDebugPinnedActor = nullptr;
	LightDebugPinnedLightComponent = nullptr;
}

AActor* FPropertyWindow::ResolvePropertyActorForLightDebug(FEditorEngine* Engine, AActor* EditorSelectedActor)
{
	FRenderer* Renderer = Engine ? Engine->GetRenderer() : nullptr;
	FShadowRenderFeature* ShadowFeature = Renderer ? Renderer->GetShadowFeature() : nullptr;

	if (!ShadowFeature ||
	    ShadowFeature->GetDebugViewMode() == EShadowDebugViewMode::None ||
	    !ShadowFeature->IsDebugViewportOverlayEnabled())
	{
		ClearLightDebugPin();
		return EditorSelectedActor;
	}

	if (LightDebugPinnedActor &&
		LightDebugPinnedLightComponent &&
		IsComponentOwnedByActor(LightDebugPinnedActor, LightDebugPinnedLightComponent))
	{
		return LightDebugPinnedActor;
	}

	ClearLightDebugPin();
	return EditorSelectedActor;
}
