#include "pch.h"
#include "TargetActorTransformWidget.h"
#include "UI/UIManager.h"
#include "ImGui/imgui.h"
#include "Actor.h"
#include "World.h"
#include "Vector.h"
#include "GizmoActor.h"
#include <string>

#include "BillboardComponent.h"
#include "StaticMeshActor.h"    
#include "StaticMeshComponent.h"
#include "ResourceManager.h"    
#include "SceneComponent.h"    
#include "TextRenderComponent.h"    
#include "DecalComponent.h"    
#include <filesystem>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

//// UE_LOG 대체 매크로
//#define UE_LOG(fmt, ...)

// 파일명 스템(Cube 등) 추출 + .obj 확장자 제거
static inline FString GetBaseNameNoExt(const FString& Path)
{
	const size_t sep = Path.find_last_of("/\\");
	const size_t start = (sep == FString::npos) ? 0 : sep + 1;

	const FString ext = ".obj";
	size_t end = Path.size();
	if (end >= ext.size() && Path.compare(end - ext.size(), ext.size(), ext) == 0)
	{
		end -= ext.size();
	}
	if (start <= end) return Path.substr(start, end - start);
	return Path;
}

// Editor/Icon 폴더에서 모든 .dds 파일을 동적으로 찾아서 반환
static TArray<FString> GetIconFiles()
{
	TArray<FString> iconFiles;
	try
	{
		fs::path iconPath = "Editor/Icon";
		if (fs::exists(iconPath) && fs::is_directory(iconPath))
		{
			for (const auto& entry : fs::directory_iterator(iconPath))
			{
				if (entry.is_regular_file())
				{
					auto filename = entry.path().filename().string();
					// .dds 확장자만 포함
					if (filename.ends_with(".dds"))
					{
						// 상대경로 포맷으로 저장 (Editor/Icon/filename.dds)
						FString relativePath = "Editor/Icon/" + filename;
						iconFiles.push_back(relativePath);
					}
				}
			}
		}
	}
	catch (const std::exception&)
	{
		// 파일 시스템 오류 발생 시 기본값으로 폴백
		iconFiles.push_back("Editor/Icon/Pawn_64x.dds");
		iconFiles.push_back("Editor/Icon/PointLight_64x.dds");
		iconFiles.push_back("Editor/Icon/SpotLight_64x.dds");
	}
	return iconFiles;
}

UTargetActorTransformWidget::UTargetActorTransformWidget()
	: UWidget("Target Actor Transform Widget")
	, UIManager(&UUIManager::GetInstance())
{

}

UTargetActorTransformWidget::~UTargetActorTransformWidget() = default;

void UTargetActorTransformWidget::OnSelectedActorCleared()
{
	// 즉시 내부 캐시/플래그 정리
	SelectedActor = nullptr;
	CachedActorName.clear();
	ResetChangeFlags();
}

void UTargetActorTransformWidget::Initialize()
{
	// UIManager 참조 확보
	UIManager = &UUIManager::GetInstance();

	// Transform 위젯을 UIManager에 등록하여 선택 해제 브로드캐스트를 받을 수 있게 함
	if (UIManager)
	{
		UIManager->RegisterTargetTransformWidget(this);
	}
}

AActor* UTargetActorTransformWidget::GetCurrentSelectedActor() const
{
	if (!UIManager)
		return nullptr;

	return UIManager->GetSelectedActor();
}

void UTargetActorTransformWidget::Update()
{
	// UIManager를 통해 현재 선택된 액터 가져오기
	AActor* CurrentSelectedActor = GetCurrentSelectedActor();
	if (SelectedActor != CurrentSelectedActor)
	{
		SelectedActor = CurrentSelectedActor;
		// 새로 선택된 액터의 이름 캐시
		if (SelectedActor)
		{
			try
			{
				// 새로운 액터가 선택되면, 선택된 컴포넌트를 해당 액터의 루트 컴포넌트로 초기화합니다.
				SelectedComponent = SelectedActor->GetRootComponent();

				CachedActorName = SelectedActor->GetName().ToString();
			}
			catch (...)
			{
				CachedActorName = "[Invalid Actor]";
				SelectedActor = nullptr;
				SelectedComponent = nullptr;
			}
		}
		else
		{
			CachedActorName = "";
			SelectedComponent = nullptr;
		}
	}

	if (SelectedActor)
	{
		// 액터가 선택되어 있으면 항상 트랜스폼 정보를 업데이트하여
		// 기즈모 조작을 실시간으로 UI에 반영합니다.
		UpdateTransformFromActor();
	}
}

/**
 * @brief Actor 복제 테스트 함수
 */
void UTargetActorTransformWidget::DuplicateTarget() const
{
	if (SelectedActor)
	{
		AActor* NewActor = Cast<AActor>(SelectedActor->Duplicate());
		
		// 초기 트랜스폼 적용
		NewActor->SetActorTransform(SelectedActor->GetActorTransform());

		// TODO(KHJ): World 접근?
		UWorld* World = SelectedActor->GetWorld();
		
		World->SpawnActor(NewActor);
	}
}

void UTargetActorTransformWidget::RenderWidget()
{
	if (SelectedActor)
	{
		// 액터 이름 표시 (캐시된 이름 사용)
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Selected: %s", CachedActorName.c_str());
		// 선택된 액터 UUID 표시(전역 고유 ID)
		ImGui::Text("UUID: %u", static_cast<unsigned int>(SelectedActor->UUID));
		ImGui::Spacing();

		// 추가 가능한 컴포넌트 타입 목록 (임시 하드코딩)
		static const TArray<TPair<FString, UClass*>> AddableComponentTypes = {
			{ "StaticMesh Component", UStaticMeshComponent::StaticClass() },
			{ "Text Component", UTextRenderComponent::StaticClass() },
			{ "Scene Component", USceneComponent::StaticClass() },
			{ "Billboard Component", UBillboardComponent::StaticClass() },
			{ "Decal Component", UDecalComponent::StaticClass() }
		};

		// 컴포넌트 추가 메뉴
		if (SelectedComponent)
		{
			if (ImGui::Button("+추가"))
			{
				ImGui::OpenPopup("AddComponentPopup");
			}

			ImGui::SameLine();

			if (ImGui::Button("-삭제"))
			{
				// 컴포넌트 삭제 시 상위 컴포넌트로 선택되도록 설정
				USceneComponent* ParentComponent = SelectedComponent->GetAttachParent();
				if (SelectedActor->DeleteComponent(SelectedComponent))
				{
					if (ParentComponent)
					{
						SelectedComponent = ParentComponent;
					}
					else
					{
						SelectedComponent = SelectedActor->GetRootComponent();
					}
				}
			}

			// "Add Component" 버튼에 대한 팝업 메뉴 정의
			if (ImGui::BeginPopup("AddComponentPopup"))
			{
				ImGui::BeginChild("ComponentListScroll", ImVec2(200.0f, 150.0f), true);

				// 추가 가능한 컴포넌트 타입 목록 메뉴 표시
				for (const TPair<FString, UClass*>& Item : AddableComponentTypes)
				{
					if (ImGui::Selectable(Item.first.c_str()))
					{
						// 컴포넌트를 누르면 생성 함수를 호출합니다.
						USceneComponent* NewSceneComponent = SelectedActor->CreateAndAttachComponent(SelectedComponent, Item.second);
						// SelectedComponent를 생성된 컴포넌트로 교체합니다
						SelectedComponent = NewSceneComponent;
						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::EndChild();

				ImGui::EndPopup();
			}
		}

		// 컴포넌트 계층 구조 표시
		ImGui::BeginChild("ComponentHierarchy", ImVec2(0, 240), true);
		if (SelectedActor)
		{
			const FName ActorName = SelectedActor->GetName();

			// 1. 최상위 액터 노드는 클릭해도 접을 수 없습니다.
			ImGui::TreeNodeEx(
				ActorName.ToString().c_str(),
				ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
			);

			// 2. 수동으로 들여쓰기를 추가합니다.
			ImGui::Indent();

			// 3. 하위 컴포넌트를 조건 없이 항상 그립니다.
			USceneComponent* RootComponent = SelectedActor->GetRootComponent();
			if (RootComponent)
			{
				RenderComponentHierarchy(RootComponent);
			}

			// 4. 들여쓰기를 해제합니다.
			ImGui::Unindent();
		}
		else
		{
			ImGui::Text("No actor selected.");
		}
		ImGui::EndChild();

		// Location 편집
		if (ImGui::DragFloat3("Location", &EditLocation.X, 0.1f))
		{
			bPositionChanged = true;
		}

		// Rotation 편집 (Euler angles)
		if (ImGui::DragFloat3("Rotation", &EditRotation.X, 0.5f))
		{
			bRotationChanged = true;
		}

		// Scale 편집
		ImGui::Checkbox("Uniform Scale", &bUniformScale);

		if (bUniformScale)
		{
			float UniformScale = EditScale.X;
			if (ImGui::DragFloat("Scale", &UniformScale, 0.01f, 0.01f, 10.0f))
			{
				EditScale = FVector(UniformScale, UniformScale, UniformScale);
				bScaleChanged = true;
			}
		}
		else
		{
			if (ImGui::DragFloat3("Scale", &EditScale.X, 0.01f, 0.01f, 10.0f))
			{
				bScaleChanged = true;
			}
		}

		ImGui::Spacing();

		// 실시간 적용 버튼
		if (ImGui::Button("Apply Transform"))
		{
			ApplyTransformToActor();
		}

		ImGui::SameLine();
		if (ImGui::Button("Reset Transform"))
		{
			UpdateTransformFromActor();
			ResetChangeFlags();
		}
		
		// TODO(KHJ): 테스트용, 완료 후 지울 것
		if (ImGui::Button("Duplicate Test Button"))
		{
			DuplicateTarget();
		}
		
		ImGui::Spacing();
		ImGui::Separator();

		// NOTE: 추후 컴포넌트별 위젯 따로 추가
		// Actor가 AStaticMeshActor인 경우 StaticMesh 변경 UI
		if (SelectedComponent)
		{
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(SelectedComponent))
			{
				ImGui::Text("Static Mesh Override");
				if (!SMC)
				{
					ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "StaticMeshComponent not found.");
				}
				else
				{
					// 현재 메시 경로 표시
					FString CurrentPath;
					UStaticMesh* CurMesh = SMC->GetStaticMesh();
					if (CurMesh)
					{
						CurrentPath = CurMesh->GetAssetPathFileName();
						ImGui::Text("Current: %s", CurrentPath.c_str());
					}
					else
					{
						ImGui::Text("Current: <None>");
					}

					// 리소스 매니저에서 로드된 모든 StaticMesh 경로 수집
					auto& RM = UResourceManager::GetInstance();
					TArray<FString> Paths = RM.GetAllStaticMeshFilePaths();

					if (Paths.empty())
					{
						ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "No StaticMesh resources loaded.");
					}
					else
					{
						// 표시용 이름(파일명 스템)
						TArray<FString> DisplayNames;
						DisplayNames.reserve(Paths.size());
						for (const FString& p : Paths)
							DisplayNames.push_back(GetBaseNameNoExt(p));

						// ImGui 콤보 아이템 배열
						TArray<const char*> Items;
						Items.reserve(DisplayNames.size());
						for (const FString& n : DisplayNames)
							Items.push_back(n.c_str());

						// 선택 인덱스 유지
						static int SelectedMeshIdx = -1;

						// 기본 선택: Cube가 있으면 자동 선택
						if (SelectedMeshIdx == -1)
						{
							for (int i = 0; i < static_cast<int>(Paths.size()); ++i)
							{
								if (DisplayNames[i] == "Cube" || Paths[i] == "Data/Cube.obj")
								{
									SelectedMeshIdx = i;
									break;
								}
							}
						}

						ImGui::SetNextItemWidth(240);
						ImGui::Combo("StaticMesh", &SelectedMeshIdx, Items.data(), static_cast<int>(Items.size()));
						ImGui::SameLine();
						if (ImGui::Button("Apply Mesh"))
						{
							if (SelectedMeshIdx >= 0 && SelectedMeshIdx < static_cast<int>(Paths.size()))
							{
								const FString& NewPath = Paths[SelectedMeshIdx];
								SMC->SetStaticMesh(NewPath);

								// Sphere 충돌 특례
								if (AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(SelectedActor))
								{
									if (GetBaseNameNoExt(NewPath) == "Sphere")
										SMActor->SetCollisionComponent(EPrimitiveType::Sphere);
									else
										SMActor->SetCollisionComponent();
								}

								UE_LOG("Applied StaticMesh: %s", NewPath.c_str());
							}
						}

						// 현재 메시로 선택 동기화 버튼 (옵션)
						ImGui::SameLine();
						if (ImGui::Button("Select Current"))
						{
							SelectedMeshIdx = -1;
							if (!CurrentPath.empty())
							{
								for (int i = 0; i < static_cast<int>(Paths.size()); ++i)
								{
									if (Paths[i] == CurrentPath ||
										DisplayNames[i] == GetBaseNameNoExt(CurrentPath))
									{
										SelectedMeshIdx = i;
										break;
									}
								}
							}
						}
					}

					// Material 설정

					const TArray<FString> MaterialNames = UResourceManager::GetInstance().GetAllFilePaths<UMaterial>();
					// ImGui 콤보 아이템 배열
					TArray<const char*> MaterialNamesCharP;
					MaterialNamesCharP.reserve(MaterialNames.size());
					for (const FString& n : MaterialNames)
						MaterialNamesCharP.push_back(n.c_str());

					if (CurMesh)
					{
						const uint64 MeshGroupCount = CurMesh->GetMeshGroupCount();

						if (0 < MeshGroupCount)
						{
							ImGui::Separator();
						}

						static TArray<int32> SelectedMaterialIdxAt; // i번 째 Material Slot이 가지고 있는 MaterialName이 MaterialNames의 몇번쩨 값인지.
						if (SelectedMaterialIdxAt.size() < MeshGroupCount)
						{
							SelectedMaterialIdxAt.resize(MeshGroupCount);
						}

						// 현재 SMC의 MaterialSlots 정보를 UI에 반영
						const TArray<FMaterialSlot>& MaterialSlots = SMC->GetMaterailSlots();
						for (uint64 MaterialSlotIndex = 0; MaterialSlotIndex < MeshGroupCount; ++MaterialSlotIndex)
						{
							for (uint32 MaterialIndex = 0; MaterialIndex < MaterialNames.size(); ++MaterialIndex)
							{
								if (MaterialSlots[MaterialSlotIndex].MaterialName == MaterialNames[MaterialIndex])
								{
									SelectedMaterialIdxAt[MaterialSlotIndex] = MaterialIndex;
								}
							}
						}

						// Material 선택
						for (uint64 MaterialSlotIndex = 0; MaterialSlotIndex < MeshGroupCount; ++MaterialSlotIndex)
						{
							ImGui::PushID(static_cast<int>(MaterialSlotIndex));
							if (ImGui::Combo("Material", &SelectedMaterialIdxAt[MaterialSlotIndex], MaterialNamesCharP.data(), static_cast<int>(MaterialNamesCharP.size())))
							{
								SMC->SetMaterialByUser(static_cast<uint32>(MaterialSlotIndex), MaterialNames[SelectedMaterialIdxAt[MaterialSlotIndex]]);
							}
							ImGui::PopID();
						}
					}
			}
		}
			// Billboard Component가 선택된 경우 Sprite UI
			else if (UBillboardComponent* BBC = Cast<UBillboardComponent>(SelectedComponent))
			{
				ImGui::Separator();
				ImGui::Text("Billboard Component Settings");
				
				// Sprite 텍스처 경로 표시 및 변경
				FString CurrentTexture = BBC->GetTexturePath();
				ImGui::Text("Current Sprite: %s", CurrentTexture.c_str());
				
				// Editor/Icon 폴더에서 동적으로 스프라이트 옵션 로드
				static TArray<FString> SpriteOptions;
				static bool bSpriteOptionsLoaded = false;
				static int currentSpriteIndex = 0; // 현재 선택된 스프라이트 인덱스
				
				if (!bSpriteOptionsLoaded)
				{
					// Editor/Icon 폴더에서 .dds 파일들을 찾아서 추가
					SpriteOptions = GetIconFiles();
					bSpriteOptionsLoaded = true;
					
					// 현재 텍스처와 일치하는 인덱스 찾기
					FString currentTexturePath = BBC->GetTexturePath();
					for (int i = 0; i < SpriteOptions.size(); ++i)
					{
						if (SpriteOptions[i] == currentTexturePath)
						{
							currentSpriteIndex = i;
							break;
						}
					}
				}
				
				// 스프라이트 선택 드롭다운 메뉴
				ImGui::Text("Sprite Texture:");
				FString currentDisplayName = (currentSpriteIndex >= 0 && currentSpriteIndex < SpriteOptions.size()) 
					? GetBaseNameNoExt(SpriteOptions[currentSpriteIndex]) 
					: "Select Sprite";
				
				if (ImGui::BeginCombo("##SpriteCombo", currentDisplayName.c_str()))
				{
					for (int i = 0; i < SpriteOptions.size(); ++i)
					{
						FString displayName = GetBaseNameNoExt(SpriteOptions[i]);
						bool isSelected = (currentSpriteIndex == i);
						
						if (ImGui::Selectable(displayName.c_str(), isSelected))
						{
							currentSpriteIndex = i;
							BBC->SetTexture(SpriteOptions[i]);
						}
						
						// 현재 선택된 항목에 포커스 설정
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				
				// 새로고침 버튼 (같은 줄에)
				ImGui::SameLine();
				if (ImGui::Button("Refresh"))
				{
					bSpriteOptionsLoaded = false; // 다음에 다시 로드하도록
					currentSpriteIndex = 0; // 인덱스 리셋
				}
				
				ImGui::Spacing();
				
				// Screen Size Scaled 체크박스
				// bool bIsScreenSizeScaled = BBC->IsScreenSizeScaled();
				// if (ImGui::Checkbox("Is Screen Size Scaled", &bIsScreenSizeScaled))
				// {
				// 	BBC->SetScreenSizeScaled(bIsScreenSizeScaled);
				// }
				
				// Screen Size (Is Screen Size Scaled가 true일 때만 활성화)
				if (false) // (bIsScreenSizeScaled)
				{
					float screenSize = BBC->GetScreenSize();
					if (ImGui::DragFloat("Screen Size", &screenSize, 0.0001f, 0.0001f, 0.1f, "%.4f"))
					{
						BBC->SetScreenSize(screenSize);
					}
				}
				else
				{
					// Billboard Size (Is Screen Size Scaled가 false일 때)
					float billboardWidth = BBC->GetBillboardWidth();
					float billboardHeight = BBC->GetBillboardHeight();
					
					if (ImGui::DragFloat("Width", &billboardWidth, 0.1f, 0.1f, 100.0f))
					{
						BBC->SetBillboardSize(billboardWidth, billboardHeight);
					}
					
					if (ImGui::DragFloat("Height", &billboardHeight, 0.1f, 0.1f, 100.0f))
					{
						BBC->SetBillboardSize(billboardWidth, billboardHeight);
					}
				}
				
				ImGui::Spacing();
				
				// UV 좌표 설정
				ImGui::Text("UV Coordinates");
				
				float u = BBC->GetU();
				float v = BBC->GetV();
				float ul = BBC->GetUL();
				float vl = BBC->GetVL();
				
				bool uvChanged = false;
				
				if (ImGui::DragFloat("U", &u, 0.01f))
					uvChanged = true;
					
				if (ImGui::DragFloat("V", &v, 0.01f))
					uvChanged = true;
					
				if (ImGui::DragFloat("UL", &ul, 0.01f))
					uvChanged = true;
					
				if (ImGui::DragFloat("VL", &vl, 0.01f))
					uvChanged = true;
				
				if (uvChanged)
				{
					BBC->SetUVCoords(u, v, ul, vl);
				}
			}
			else if (UTextRenderComponent* TextRenderComponent = Cast<UTextRenderComponent>(SelectedComponent))
			{
				ImGui::Separator();
				ImGui::Text("TextRender Component Settings");

				static char textBuffer[256];
				static UTextRenderComponent* lastSelected = nullptr;
				if (lastSelected != TextRenderComponent)
				{
					strncpy_s(textBuffer, sizeof(textBuffer), TextRenderComponent->GetText().c_str(), sizeof(textBuffer) - 1);
					lastSelected = TextRenderComponent;
				}

				ImGui::Text("Text Content");

				if (ImGui::InputText("##TextContent", textBuffer, sizeof(textBuffer)))
				{
					// 실시간으로 SetText 함수 호출
					TextRenderComponent->SetText(FString(textBuffer));
				}

				ImGui::Spacing();

				//// 4. 텍스트 색상을 편집하는 Color Picker를 추가합니다.
				//FLinearColor currentColor = TextRenderComponent->GetTextColor();
				//float color[3] = { currentColor.R, currentColor.G, currentColor.B }; // ImGui는 float 배열 사용

				//ImGui::Text("Text Color");
				//if (ImGui::ColorEdit3("##TextColor", color))
				//{
				//	// 색상이 변경되면 컴포넌트의 SetTextColor 함수를 호출
				//	TextRenderComponent->SetTextColor(FLinearColor(color[0], color[1], color[2]));
				//}
			}
			else if (UDecalComponent* UDC = Cast<UDecalComponent>(SelectedComponent))
			{
				ImGui::Separator();
				ImGui::Text("데칼 Component Settings");

				// Sprite 텍스처 경로 표시 및 변경
				FString CurrentTexture = UDC->GetTexturePath();
				ImGui::Text("Current Sprite: %s", CurrentTexture.c_str());

				// Editor/Icon 폴더에서 동적으로 스프라이트 옵션 로드
				static TArray<FString> SpriteOptions;
				static bool bSpriteOptionsLoaded = false;
				static int currentSpriteIndex = 0; // 현재 선택된 스프라이트 인덱스

				if (!bSpriteOptionsLoaded)
				{
					// Editor/Icon 폴더에서 .dds 파일들을 찾아서 추가
					SpriteOptions = GetIconFiles();
					bSpriteOptionsLoaded = true;

					// 현재 텍스처와 일치하는 인덱스 찾기
					FString currentTexturePath = UDC->GetTexturePath();
					for (int i = 0; i < SpriteOptions.size(); ++i)
					{
						if (SpriteOptions[i] == currentTexturePath)
						{
							currentSpriteIndex = i;
							break;
						}
					}
				}

				// 스프라이트 선택 드롭다운 메뉴
				ImGui::Text("Sprite Texture:");
				FString currentDisplayName = (currentSpriteIndex >= 0 && currentSpriteIndex < SpriteOptions.size())
					? GetBaseNameNoExt(SpriteOptions[currentSpriteIndex])
					: "Select Sprite";

				if (ImGui::BeginCombo("##SpriteCombo", currentDisplayName.c_str()))
				{
					for (int i = 0; i < SpriteOptions.size(); ++i)
					{
						FString displayName = GetBaseNameNoExt(SpriteOptions[i]);
						bool isSelected = (currentSpriteIndex == i);

						if (ImGui::Selectable(displayName.c_str(), isSelected))
						{
							currentSpriteIndex = i;
							UDC->SetTexturePath(SpriteOptions[i]);
						}

						// 현재 선택된 항목에 포커스 설정
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				// 새로고침 버튼 (같은 줄에)
				ImGui::SameLine();
				if (ImGui::Button("Refresh"))
				{
					bSpriteOptionsLoaded = false; // 다음에 다시 로드하도록
					currentSpriteIndex = 0; // 인덱스 리셋
				}

				ImGui::Spacing();

				ImGui::Separator();
				ImGui::Text("Fade Settings");

				// 1. UDecalComponent에서 현재 FadeAlpha 값을 가져옵니다.
				//    (이 값을 슬라이더와 연동하기 위한 임시 변수입니다.)
				float currentAlpha = UDC->GetFadeAlpha();

				// 2. "투명도 (Fade)"라는 이름으로 0.0f ~ 1.0f 범위의 슬라이더를 만듭니다.
				//    사용자가 슬라이더를 조작하여 currentAlpha 값이 변경되면, SliderFloat 함수는 true를 반환합니다.
				if (ImGui::SliderFloat("투명도 (Fade)", &currentAlpha, 0.0f, 1.0f))
				{
					// 3. 슬라이더 값이 변경되었을 경우에만 컴포넌트의 실제 값을 업데이트합니다.
					UDC->SetFadeAlpha(currentAlpha);
				}
			}

		else
		{
			ImGui::Text("Selected component is not a supported type.");
		}
		}
	}
	else
	{
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No Actor Selected");
		ImGui::TextUnformatted("Select an actor to edit its transform.");
	}

	ImGui::Separator();
}

// 재귀적으로 모든 하위 컴포넌트를 트리 형태로 렌더링
void UTargetActorTransformWidget::RenderComponentHierarchy(USceneComponent* SceneComponent)
{
	if (!SceneComponent)
	{
		return;
	}

	if (!SelectedActor || !SelectedComponent)
	{
		return;
	}

	const bool bIsRootComponent = SelectedActor->GetRootComponent() == SceneComponent;
	const FString ComponentName = SceneComponent->GetName() + (bIsRootComponent ? " (Root)" : "");
	const TArray<USceneComponent*>& AttachedChildren = SceneComponent->GetAttachChildren();
	const bool bHasChildren = AttachedChildren.Num() > 0;

	ImGuiTreeNodeFlags NodeFlags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_DefaultOpen;

	// 현재 그리고 있는 SceneComponent가 SelectedComponent와 일치하는지 확인
	const bool bIsSelected = (SelectedComponent == SceneComponent);
	if (bIsSelected)
	{
		// 일치하면 Selected 플래그를 추가하여 하이라이트 효과를 줍니다.
		NodeFlags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!bHasChildren)
	{
		NodeFlags |= ImGuiTreeNodeFlags_Leaf;
	}

	const bool bNodeIsOpen = ImGui::TreeNodeEx(
		(void*)SceneComponent,
		NodeFlags,
		"%s",
		ComponentName.c_str()
	);

	// 방금 그린 TreeNode가 클릭되었는지 확인합니다.
	if (ImGui::IsItemClicked())
	{
		// 클릭되었다면, 멤버 변수인 SelectedComponent를 현재 컴포넌트로 업데이트합니다.
		SelectedComponent = SceneComponent;
	}

	if (bNodeIsOpen)
	{
		for (USceneComponent* ChildComponent : AttachedChildren)
		{
			RenderComponentHierarchy(ChildComponent);
		}
		ImGui::TreePop();
	}
}

void UTargetActorTransformWidget::PostProcess()
{
	// 자동 적용이 활성화된 경우 변경사항을 즉시 적용
	if (bPositionChanged || bRotationChanged || bScaleChanged)
	{
		ApplyTransformToActor();
		ResetChangeFlags(); // 적용 후 플래그 리셋
	}
}

void UTargetActorTransformWidget::UpdateTransformFromActor()
{
	if (!SelectedActor)
		return;

	if (SelectedComponent)
	{
		// 액터의 현재 트랜스폼을 UI 변수로 복사
		EditLocation = SelectedComponent->GetRelativeLocation();
		EditRotation = SelectedComponent->GetRelativeRotation().ToEuler();
		EditScale = SelectedComponent->GetRelativeScale();
	}

	//// 균등 스케일 여부 판단
	//bUniformScale = (abs(EditScale.X - EditScale.Y) < 0.01f &&
	//	abs(EditScale.Y - EditScale.Z) < 0.01f);

	ResetChangeFlags();
}

void UTargetActorTransformWidget::ApplyTransformToActor() const
{
	if (!SelectedActor || !SelectedComponent)
		return;

	// 변경사항이 있는 경우에만 적용
	if (bPositionChanged)
	{
		SelectedComponent->SetRelativeLocation(EditLocation);
		UE_LOG("Transform: Applied location (%.2f, %.2f, %.2f)",
			EditLocation.X, EditLocation.Y, EditLocation.Z);
	}

	if (bRotationChanged)
	{
		FQuat NewRotation = FQuat::MakeFromEuler(EditRotation);
		SelectedComponent->SetRelativeRotation(NewRotation);
		UE_LOG("Transform: Applied rotation (%.1f, %.1f, %.1f)",
			EditRotation.X, EditRotation.Y, EditRotation.Z);
	}

	if (bScaleChanged)
	{
		SelectedComponent->SetRelativeScale(EditScale);
		UE_LOG("Transform: Applied scale (%.2f, %.2f, %.2f)",
			EditScale.X, EditScale.Y, EditScale.Z);
	}

	// 플래그 리셋은 const 메서드에서 할 수 없으므로 PostProcess에서 처리
}

void UTargetActorTransformWidget::ResetChangeFlags()
{
	bPositionChanged = false;
	bRotationChanged = false;
	bScaleChanged = false;
}
