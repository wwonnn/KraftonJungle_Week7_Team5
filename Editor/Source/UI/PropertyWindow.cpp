#include "PropertyWindow.h"
#include "EditorEngine.h"
#include "Actor/Actor.h"
#include "Actor/SpotLightFakeActor.h"
#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/RandomColorComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/LocalHeightFogComponent.h"
#include "Component/MoveComponent.h"
#include "Component/DecalComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/MovementComponent.h"
#include "Component/RotatingMovementComponent.h"
#include "Component/ProjectileMovementComponent.h"
#include "Level/Level.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Object/ObjectIterator.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Core/Paths.h"
#include "Math/LinearColor.h"

#include <algorithm>

#include "Renderer/Features/Billboard/BillboardRenderer.h"

namespace
{
	using FComponentClassGetter = UClass * (*)();

	struct FComponentAddOption
	{
		const char*           Label;
		const char*           BaseName;
		FComponentClassGetter GetClass;
	};

	const FComponentAddOption GComponentAddOptions[] =
	{
		{"Scene Component", "SceneComponent", &USceneComponent::StaticClass},
		{"Static Mesh Component", "StaticMeshComponent", &UStaticMeshComponent::StaticClass},
		{"Text Component", "TextComponent", &UTextRenderComponent::StaticClass},
		{"SubUV Component", "SubUVComponent", &USubUVComponent::StaticClass},
		{"BillboardComponent", "BillboardComponent", &UBillboardComponent::StaticClass},
		{"Move Component", "MoveComponent", &UMoveComponent::StaticClass},
		{"Rotating Movement Component", "RotatingMovementComponent", &URotatingMovementComponent::StaticClass},
		{"Projectile Movement Component", "ProjectileMovementComponent", &UProjectileMovementComponent::StaticClass},
		{"FireBall Component", "FireBallComponent", &UFireBallComponent::StaticClass},
		{"Decal Component", "DecalComponent", &UDecalComponent::StaticClass},
		{"Local Height Fog Component", "LocalHeightFogComponent", &ULocalHeightFogComponent::StaticClass},
	};

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

	bool EditLinearColor4(const char* Label, FVector4& InOutColor, ImGuiColorEditFlags Flags = 0)
	{
		const FVector4 DisplayColor = FLinearColor::LinearToSRGB(InOutColor);
		float ColorArray[4] = { DisplayColor.X, DisplayColor.Y, DisplayColor.Z, DisplayColor.W };
		if (!ImGui::ColorEdit4(
			Label,
			ColorArray,
			Flags | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_HDR))
		{
			return false;
		}

		InOutColor = FLinearColor::SRGBToLinear(FVector4(ColorArray[0], ColorArray[1], ColorArray[2], ColorArray[3]));
		return true;
	}

	bool EditLinearColor4(const char* Label, FLinearColor& InOutColor, ImGuiColorEditFlags Flags = 0)
	{
		FVector4 LinearColor = InOutColor.ToVector4();
		if (!EditLinearColor4(Label, LinearColor, Flags))
		{
			return false;
		}

		InOutColor = FLinearColor(LinearColor.X, LinearColor.Y, LinearColor.Z, LinearColor.W);
		return true;
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

	AActor* SelectedActor = Engine ? Engine->GetSelectedActor() : nullptr;
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
						TArray<FString> MatNames    = FMaterialManager::Get().GetAllMaterialNames();
						uint32          NumSections = MeshData->GetNumSections();

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
								ImTextureID TexID        = 0; // 빨간줄 방지용 0 캐스팅

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
							ImGui::EndCombo();
						}
						ImGui::PopItemWidth();

						float MasterScroll[4] = {0.0f, 0.0f, 0.0f, 0.0f};

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
									ImTextureID TexID        = 0; // 빨간줄 방지용 0 캐스팅

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
								ImGui::EndCombo();
							}

							if (CurrentMat)
							{
								ImGui::PushID(i + 1000);
								FVector4 MatColor = CurrentMat->GetVectorParameter("BaseColor");
								if (EditLinearColor4("Base Color", MatColor))
								{
									CurrentMat->SetParameterData("BaseColor", &MatColor, sizeof(MatColor));
								}
								ImGui::PopID();

								if (auto MatTex = CurrentMat->GetMaterialTexture())
								{
									float SpeedArray[4] = {0.0f, 0.0f, 0.0f, 0.0f};
									CurrentMat->GetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));

									ImGui::PushID(i + 2000);
									// 마찬가지로 UI 조작은 X, Y 2개만 합니다.
									if (ImGui::DragFloat2("UV Scroll", SpeedArray, 0.001f, -5.0f, 5.0f, "%.2f"))
									{
										CurrentMat->SetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));
									}
									ImGui::PopID();
								}
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

void FPropertyWindow::SetTarget(const FVector& Location, const FVector& Rotation,
                                const FVector& Scale, const char*       ActorName)
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

	if (Component->IsA(UStaticMeshComponent::StaticClass()))
	{
		DrawStaticMeshComponentDetails(static_cast<UStaticMeshComponent*>(Component));
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

	if (Component->IsA(UDecalComponent::StaticClass()))
	{
		DrawDecalComponentDetails(static_cast<UDecalComponent*>(Component), Engine);
	}

	if (Component->IsA(UFireBallComponent::StaticClass()))
	{
		DrawFireBallComponentDetails(static_cast<UFireBallComponent*>(Component));
	}
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


void FPropertyWindow::DrawStaticMeshComponentDetails(UStaticMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Static Mesh");

	UStaticMesh*      CurrentMesh     = MeshComponent->GetStaticMesh();
	const std::string CurrentMeshName = CurrentMesh ? CurrentMesh->GetAssetPathFileName() : "None";

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

			const std::string MeshName  = MeshAsset->GetAssetPathFileName();
			const bool        bSelected = (CurrentMesh == MeshAsset);
			if (ImGui::Selectable(MeshName.c_str(), bSelected))
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

	TArray<FString> MaterialNames = FMaterialManager::Get().GetAllMaterialNames();
	const uint32    NumSections   = CurrentMesh->GetNumSections();

	if (ImGui::BeginCombo("Apply To All", "Select Material..."))
	{
		for (const FString& MaterialName : MaterialNames)
		{
			if (ImGui::Selectable(MaterialName.c_str(), false))
			{
				if (std::shared_ptr<FMaterial> Material = FMaterialManager::Get().FindByName(MaterialName))
				{
					for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
					{
						MeshComponent->SetMaterial(SectionIndex, Material);
					}
				}
			}
		}
		ImGui::EndCombo();
	}

	float MasterScroll[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
		const std::string          CurrentMaterialName = CurrentMaterial ? CurrentMaterial->GetOriginName() : "None";
		const std::string          ComboLabel          = "Section " + std::to_string(SectionIndex);

		ImGui::PushID(static_cast<int>(SectionIndex));
		ImGui::PushItemWidth(-1.0f);
		if (ImGui::BeginCombo(ComboLabel.c_str(), CurrentMaterialName.c_str()))
		{
			for (const FString& MaterialName : MaterialNames)
			{
				const bool bSelected = (CurrentMaterialName == MaterialName);
				if (ImGui::Selectable(MaterialName.c_str(), bSelected))
				{
					if (std::shared_ptr<FMaterial> Material = FMaterialManager::Get().FindByName(MaterialName))
					{
						MeshComponent->SetMaterial(SectionIndex, Material);
						CurrentMaterial = Material;
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

		if (CurrentMaterial)
		{
			FVector4 BaseColor = CurrentMaterial->GetVectorParameter("BaseColor");
			if (EditLinearColor4("Base Color", BaseColor))
			{
				CurrentMaterial->SetParameterData("BaseColor", &BaseColor, sizeof(BaseColor));
			}

			float ScrollArray[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			CurrentMaterial->GetParameterData("UVScrollSpeed", ScrollArray, sizeof(ScrollArray));
			if (ImGui::DragFloat2("UV Scroll", ScrollArray, 0.001f, -5.0f, 5.0f, "%.2f"))
			{
				CurrentMaterial->SetParameterData("UVScrollSpeed", ScrollArray, sizeof(ScrollArray));
			}
		}
		ImGui::Spacing();
		ImGui::PopID();
	}
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

	FVector4 TextColor = TextComponent->GetTextColor();
	if (EditLinearColor4("Text Color", TextColor))
	{
		TextComponent->SetTextColor(TextColor);
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


	const char* HAlignOptions[] = {"Left", "Center", "Right"};
	int         HAlignIndex     = TextComponent->GetHorizontalAlignment();
	if (ImGui::Combo("Horizontal Alignment", &HAlignIndex, HAlignOptions, IM_ARRAYSIZE(HAlignOptions)))
	{
		TextComponent->SetHorizontalAlignment(static_cast<EHorizTextAligment>(HAlignIndex));
	}

	const char* VAlignOptions[] = {"Top", "Center", "Bottom"};
	int         VAlignIndex     = TextComponent->GetVerticalAlignment();
	if (ImGui::Combo("Vertical Alignment", &VAlignIndex, VAlignOptions, IM_ARRAYSIZE(VAlignOptions)))
	{
		TextComponent->SetVerticalAlignment(static_cast<EVerticalTextAligment>(VAlignIndex));
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
	float    SizeArray[2] = {Size.X, Size.Y};
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

	bool AllowBackground = static_cast<bool>(HeightFogComponent->AllowBackground);
	if (ImGui::Checkbox("Allow Background", &AllowBackground))
	{
		HeightFogComponent->AllowBackground = static_cast<float>(AllowBackground);
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

	bool AllowBackground = static_cast<bool>(LocalHeightFogComponent->AllowBackground);
	if (ImGui::Checkbox("Allow Background", &AllowBackground))
	{
		LocalHeightFogComponent->AllowBackground = static_cast<float>(AllowBackground);
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
	FString      CurrentFileName = FPaths::FromPath(std::filesystem::path(CurrentPath).filename());
	if (ImGui::BeginCombo("Sprite", CurrentFileName.c_str()))
	{
		for (auto& Pair : Engine->GetRenderer()->GetBillboardRenderer().GetTextureCache())
		{
			FString FileName  = FPaths::FromPath(std::filesystem::path(Pair.first).filename());
			bool    bSelected = (Pair.first == CurrentPath);
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

	float Size[2] = {BillboardComponent->GetSize().X, BillboardComponent->GetSize().Y};
	if (ImGui::DragFloat2("Size", Size, 0.01f, 0.01f, 100.f, "%.2f"))
	{
		BillboardComponent->SetSize(FVector2(Size[0], Size[1]));
	}

	FVector4 BillboardBaseColor = BillboardComponent->GetBaseColor();
	if (EditLinearColor4("Base Color", BillboardBaseColor))
	{
		BillboardComponent->SetBaseColor(BillboardBaseColor);
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

	float Size[2] = {DecalComponent->GetSize().X, DecalComponent->GetSize().Y};
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
		                           : FPaths::FromPath(std::filesystem::path(CurrentPath).filename());

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
			const FString      Label     = FPaths::FromPath(Path.filename());
			const std::wstring FullPath  = Path.wstring();
			const bool         bSelected = (FullPath == CurrentPath);
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
		}
		ImGui::EndCombo();
	}

	FLinearColor BaseColorTint = DecalComponent->GetBaseColorTint();
	if (EditLinearColor4("Base Color Tint", BaseColorTint))
	{
		DecalComponent->SetBaseColorTint(BaseColorTint);
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

bool FPropertyWindow::DrawVector3Control(const char* Label, const FVector& Value, FVector& OutValue, float Speed, const char* Format)
{
	float Values[3] = {Value.X, Value.Y, Value.Z};
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
