#include "pch.h"
#include "SelectionManager.h"
#include "Picking.h"
#include "SceneLoader.h"
#include "CameraActor.h"
#include "StaticMeshActor.h"
#include "CameraComponent.h"
#include "ObjectFactory.h"
#include "TextRenderComponent.h"
#include "AABoundingBoxComponent.h"
#include "FViewport.h"
#include "SViewportWindow.h"
#include "SMultiViewportWindow.h"
#include "StaticMesh.h"
#include "ObjManager.h"
#include "SceneRotationUtils.h"
#include "Frustum.h"
#include "Octree.h"
#include "BVH.h"
#include"UEContainer.h"
#include"StaticMeshActor.h"
#include <DecalComponent.h>

extern float CLIENTWIDTH;
extern float CLIENTHEIGHT;

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


UWorld::UWorld() : ResourceManager(UResourceManager::GetInstance())
, UIManager(UUIManager::GetInstance())
, InputManager(UInputManager::GetInstance())
, SelectionManager(USelectionManager::GetInstance())
, BVH(nullptr)
{
	Level = NewObject<ULevel>();
}

UWorld::~UWorld()
{
	// Level의 Actors 정리 (PIE는 복제된 액터들만 삭제)
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			ObjectFactory::DeleteObject(Actor);
		}

		// Level 자체 정리
		ObjectFactory::DeleteObject(Level);
		Level = nullptr;
	}

	// PIE 월드가 아닐 때만 공유 리소스 삭제
	if (WorldType == EWorldType::Editor)
	{
		// 카메라 정리
		ObjectFactory::DeleteObject(MainCameraActor);
		MainCameraActor = nullptr;

		// Grid 정리
		ObjectFactory::DeleteObject(GridActor);
		GridActor = nullptr;

		// GizmoActor 정리
		ObjectFactory::DeleteObject(GizmoActor);
		GizmoActor = nullptr;

		// BVH 정리
		if (BVH)
		{
			delete BVH;
			BVH = nullptr;
		}

		// ObjManager 정리
		FObjManager::Clear();
	}
	else if (WorldType == EWorldType::PIE)
	{
		// PIE 월드는 공유 포인터만 nullptr로 설정 (삭제하지 않음)
		MainCameraActor = nullptr;
		GridActor = nullptr;
		GizmoActor = nullptr;
		BVH = nullptr;
		Renderer = nullptr;
		MainViewport = nullptr;
		MultiViewport = nullptr;
	}
}

static void DebugRTTI_UObject(UObject* Obj, const char* Title)
{
	if (!Obj)
	{
		UE_LOG("[RTTI] Obj == null\r\n");
		return;
	}

	char buf[256];
	UE_LOG("========== RTTI CHECK ==========\r\n");
	if (Title)
	{
		std::snprintf(buf, sizeof(buf), "[RTTI] %s\r\n", Title);
		UE_LOG(buf);
	}

	// 1) 현재 동적 타입 이름
	std::snprintf(buf, sizeof(buf), "[RTTI] TypeName = %s\r\n", Obj->GetClass()->Name);
	UE_LOG(buf);

	// 2) IsA 체크 (파생 포함)
	std::snprintf(buf, sizeof(buf), "[RTTI] IsA<AActor>      = %d\r\n", (int)Obj->IsA<AActor>());
	UE_LOG(buf);
	std::snprintf(buf, sizeof(buf), "[RTTI] IsA<ACameraActor> = %d\r\n",
		(int)Obj->IsA<ACameraActor>());
	UE_LOG(buf);

	//// 3) 정확한 타입 비교 (파생 제외)
	//std::snprintf(buf, sizeof(buf), "[RTTI] EXACT ACameraActor = %d\r\n",
	//    (int)(Obj->GetClass() == ACameraActor::StaticClass()));
	//UE_LOG(buf);

	// 4) 상속 체인 출력
	UE_LOG("[RTTI] Inheritance chain: ");
	for (const UClass* c = Obj->GetClass(); c; c = c->Super)
	{
		std::snprintf(buf, sizeof(buf), "%s%s", c->Name, c->Super ? " <- " : "\r\n");
		UE_LOG(buf);
	}
	//FString Name = Obj->GetName();
	std::snprintf(buf, sizeof(buf), "[RTTI] TypeName = %s\r\n", Obj->GetName().c_str());
	OutputDebugStringA(buf);
	OutputDebugStringA("================================\r\n");
}

void UWorld::Initialize()
{
	FObjManager::Preload();

	// 새 씬 생성
	CreateNewScene();

	InitializeMainCamera();
	InitializeGrid();
	InitializeGizmo();

	// 액터 간 참조 설정
	SetupActorReferences();
}

void UWorld::InitializeMainCamera()
{
	MainCameraActor = NewObject<ACameraActor>();

	DebugRTTI_UObject(MainCameraActor, "MainCameraActor");
	UIManager.SetCamera(MainCameraActor);

	EngineActors.Add(MainCameraActor);
}

void UWorld::InitializeGrid()
{
	GridActor = NewObject<AGridActor>();
	GridActor->Initialize();

	// Add GridActor to Actors array so it gets rendered in the main loop
	EngineActors.push_back(GridActor);
	//EngineActors.push_back(GridActor);
}

void UWorld::InitializeGizmo()
{
	// === 기즈모 엑터 초기화 ===
	GizmoActor = NewObject<AGizmoActor>();
	GizmoActor->SetWorld(this);
	GizmoActor->SetActorTransform(FTransform(FVector{ 0, 0, 0 },
		FQuat::MakeFromEuler(FVector{ 0, -90, 0 }),
		FVector{ 1, 1, 1 }));
	// 기즈모에 카메라 참조 설정
	if (MainCameraActor)
	{
		GizmoActor->SetCameraActor(MainCameraActor);
	}

	UIManager.SetGizmoActor(GizmoActor);
}

void UWorld::InitializeSceneGraph(TArray<AActor*>& Actors)
{
	Octree = NewObject<UOctree>();
	//	Octree->Initialize(FBound({ -100,-100,-100 }, { 100,100,100 }));
	//const TArray<AActor*>& InActors, FBound& WorldBounds, int32 Depth = 0
	Octree->Build(Actors, FBound({ -100, -100, -100 }, { 100, 100, 100 }), 0);

	// 빌드 완료 후 모든 마이크로 BVH 미리 생성
#ifndef _DEBUG
	Octree->PreBuildAllMicroBVH();

	// BVH 초기화 및 빌드
	BVH = new FBVH();
	BVH->Build(Actors);
#endif
}

void UWorld::RenderSceneGraph()
{
	if (!Octree)
	{
		return;
	}
	Octree->Render(nullptr);
}

void UWorld::SetRenderer(URenderer* InRenderer)
{
	Renderer = InRenderer;
}

void UWorld::Render()
{
	Renderer->BeginFrame();
	UIManager.Render();

	// UIManager의 뷰포트 전환 상태에 따라 렌더링 변경 SWidget으로 변경해줄거임

	if (MultiViewport)
	{
		MultiViewport->OnRender();
	}

	//프레임 종료 
	UIManager.EndFrame();
	Renderer->EndFrame();
}

void UWorld::RenderViewports(ACameraActor* Camera, FViewport* Viewport)
{
	// 뷰포트의 실제 크기로 aspect ratio 계산
	float ViewportAspectRatio = static_cast<float>(Viewport->GetSizeX()) / static_cast<float>(Viewport->GetSizeY());
	if (Viewport->GetSizeY() == 0)	// 0으로 나누기 방지
	{
		ViewportAspectRatio = 1.0f;
	}

	FMatrix ViewMatrix = Camera->GetViewMatrix();
	FMatrix ProjectionMatrix = Camera->GetProjectionMatrix(ViewportAspectRatio, Viewport);
	if (!Renderer)
	{
		return;
	}
	FVector rgb(1.0f, 1.0f, 1.0f);

	FFrustum ViewFrustum;
	ViewFrustum.Update(ViewMatrix * ProjectionMatrix);


	// --- 1. 씬 순회 및 프리미티브 수집 (Culling & Gathering) ---
	TArray<UPrimitiveComponent*> OpaquePrimitives;
	TArray<UDecalComponent*> DecalComponents;

	// (Frustum Culling 등은 이 단계에서 수행)
	for (AActor* Actor : Level->GetActors())
	{
		// ... 각종 Actor 활성화 및 ShowFlag 체크 ...
		if (Actor->GetActorHiddenInGame())
		{
			continue;
		}

		// 렌더링 액터 컴포넌트 수집
		for (UActorComponent* Component : Actor->GetComponents())
		{
			// @@@순서 중요@@@ 데칼부터 검사해야 됨 포함 관계라서
			if (UDecalComponent* Decal = Cast<UDecalComponent>(Component))
			{
				DecalComponents.Add(Decal); // 데칼은 데칼 목록에 추가
			}
			else if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				OpaquePrimitives.Add(Primitive); // 일반 프리미티브는 불투명 목록에 추가
			}
		}
	}

	Renderer->BeginLineBatch();
	Renderer->SetViewModeType(ViewModeIndex);

	for (UPrimitiveComponent* OpaquePrimitive : OpaquePrimitives)
	{
		bool bIsSelected = SelectionManager.IsActorSelected(OpaquePrimitive->GetOwner());

		Renderer->UpdateHighLightConstantBuffer(bIsSelected, rgb, 0, 0, 0, 0);
		OpaquePrimitive->Render(Renderer, ViewMatrix, ProjectionMatrix);

		Renderer->OMSetBlendState(false);	// Render 과정에서 blend가 true로 될 수 있음
	}

	// [임시] 선택한 데칼 OBB 외곽선 표시
	for (UDecalComponent* DecalComponent : DecalComponents)
	{
		bool bIsSelected = SelectionManager.IsActorSelected(DecalComponent->GetOwner());
		if (bIsSelected)
		{
			DecalComponent->RenderOBB(Renderer, ViewMatrix, ProjectionMatrix);
		}
	}

	// (Blend ON, Depth Test ON, Depth Write OFF 등)
	Renderer->SetDecalRenderState();
	for (UDecalComponent* DecalComponent : DecalComponents)
	{
		// 데칼 셰이더 설정
		Renderer->PrepareShader(DecalComponent->GetMaterial()->GetShader());

		// 데칼의 상수 버퍼(ViewProjection 행렬 등)를 업데이트합니다.
		Renderer->GetRHIDevice()->UpdateDecalConstantBuffers(DecalComponent->GetViewProjectionMatrix());

		Renderer->UpdateColorBuffer(FVector4(0, 0, 0, DecalComponent->GetFadeAlpha()));

		// [임시] 충돌 판정하기 귀찮아서 모든 오브젝트를 데칼과 충돌했다 판정
		TArray<UPrimitiveComponent*> TargetPrimitives = OpaquePrimitives;

		for (UPrimitiveComponent* Target : TargetPrimitives)
		{
			if (UStaticMeshComponent* USC = Cast<UStaticMeshComponent>(Target))
			{
				if (USC->GetStaticMesh())
				{
					Renderer->UpdateConstantBuffer(USC->GetWorldMatrix(), ViewMatrix, ProjectionMatrix);
					Renderer->DrawDecalIndexedPrimitiveComponent(USC->GetStaticMesh(), D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, DecalComponent);
				}
			}
		}
	}

	// 상태 복구
	Renderer->OMSetBlendState(false);
	Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);

	// 엔진 액터들 (그리드 등) 렌더링
	RenderEngineActors(ViewMatrix, ProjectionMatrix, Viewport);

	Renderer->EndLineBatch(FMatrix::Identity(), ViewMatrix, ProjectionMatrix);
}

void UWorld::RenderEngineActors(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, FViewport* Viewport)
{
	for (AActor* EngineActor : EngineActors)
	{
		if (!EngineActor)
		{
			continue;
		}

		if (EngineActor->GetActorHiddenInGame())
		{
			continue;
		}

		if (Cast<AGridActor>(EngineActor) && !Viewport->IsShowFlagEnabled(EEngineShowFlags::SF_Grid))
		{
			continue;
		}

		for (UActorComponent* Component : EngineActor->GetComponents())
		{
			if (!Component)
			{
				continue;
			}

			if (UActorComponent* ActorComp = Cast<UActorComponent>(Component))
			{
				if (!ActorComp->IsActive())
				{
					continue;
				}
			}

			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				Renderer->SetViewModeType(ViewModeIndex);
				Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
				Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
			}
		}
		Renderer->OMSetBlendState(false);
	}
}

void UWorld::Tick(float DeltaSeconds)
{
	// Level의 Actors Tick
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && Actor->IsActorTickEnabled())
			{
				Actor->Tick(DeltaSeconds);
			}
		}
	}

	// Engine Actors Tick
	for (AActor* EngineActor : EngineActors)
	{
		if (EngineActor && EngineActor->IsActorTickEnabled())
		{
			EngineActor->Tick(DeltaSeconds);
		}
	}

	if (GizmoActor)
	{
		GizmoActor->Tick(DeltaSeconds);
	}

	//ProcessActorSelection();
	ProcessViewportInput();
	//Input Manager가 카메라 후에 업데이트 되어야함

	// 뷰포트 업데이트 - UIManager의 뷰포트 전환 상태에 따라
	if (MultiViewport)
	{
		MultiViewport->OnUpdate(DeltaSeconds);
	}

	//InputManager.Update();
	UIManager.Update(DeltaSeconds);
}

float UWorld::GetTimeSeconds() const
{
	return 0.0f;
}

bool UWorld::FrustumCullActors(const FFrustum& ViewFrustum, const AActor* Actor, int& FrustumCullCount)
{
	if (Actor->CollisionComponent)
	{
		FBound Test = Actor->CollisionComponent->GetWorldBoundFromCube();

		// 절두체 밖에 있다면, 이 액터의 렌더링 과정을 모두 건너뜁니다.
		if (!ViewFrustum.IsVisible(Test))
		{
			FrustumCullCount++;
			return true;
		}
	}
}

FString UWorld::GenerateUniqueActorName(const FString& ActorType)
{
	// Get current count for this type
	int32& CurrentCount = ObjectTypeCounts[ActorType];
	FString UniqueName = ActorType + "_" + std::to_string(CurrentCount);
	CurrentCount++;
	return UniqueName;
}

//
// 액터 제거
//
bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor)
	{
		return false; // nullptr 들어옴 → 실패
	}

	// SelectionManager에서 선택 해제 (메모리 해제 전에 하자)
	USelectionManager::GetInstance().DeselectActor(Actor);

	// UIManager에서 픽된 액터 정리
	if (UIManager.GetPickedActor() == Actor)
	{
		UIManager.ResetPickedActor();
	}

	// 배열에서  제거 시도
	// Level에서 제거 시도
	if (Level)
	{
		Level->RemoveActor(Actor);

		// 메모리 해제
		ObjectFactory::DeleteObject(Actor);
		// 삭제된 액터 정리
		USelectionManager::GetInstance().CleanupInvalidActors();

		return true; // 성공적으로 삭제
	}

	return false; // 월드에 없는 액터
}

inline FString ToObjFileName(const FString& TypeName)
{
	return "Data/" + TypeName + ".obj";
}

inline FString RemoveObjExtension(const FString& FileName)
{
	const FString Extension = ".obj";

	// 마지막 경로 구분자 위치 탐색 (POSIX/Windows 모두 지원)
	const uint64 Sep = FileName.find_last_of("/\\");
	const uint64 Start = (Sep == FString::npos) ? 0 : Sep + 1;

	// 확장자 제거 위치 결정
	uint64 End = FileName.size();
	if (End >= Extension.size() &&
		FileName.compare(End - Extension.size(), Extension.size(), Extension) == 0)
	{
		End -= Extension.size();
	}

	// 베이스 이름(확장자 없는 파일명) 반환
	if (Start <= End)
	{
		return FileName.substr(Start, End - Start);
	}

	// 비정상 입력 시 원본 반환 (안전장치)
	return FileName;
}

void UWorld::CreateNewScene()
{
	// Safety: clear interactions that may hold stale pointers
	SelectionManager.ClearSelection();
	UIManager.ResetPickedActor();
	// Level의 Actors 정리
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			ObjectFactory::DeleteObject(Actor);
		}
		Level->GetActors().clear();
	}

	if (Octree)
	{
		Octree->Release();//새로운 씬이 생기면 Octree를 지워준다.
	}
	if (BVH)
	{
		BVH->Clear();//새로운 씬이 생기면 BVH를 지워준다.
	}
	// 이름 카운터 초기화: 씬을 새로 시작할 때 각 BaseName 별 suffix를 0부터 다시 시작
	ObjectTypeCounts.clear();
}

// 액터 인터페이스 관리 메소드들
void UWorld::SetupActorReferences()
{
	if (GizmoActor && MainCameraActor)
	{
		GizmoActor->SetCameraActor(MainCameraActor);
	}
}

//마우스 피킹관련 메소드
void UWorld::ProcessActorSelection()
{
	if (InputManager.IsMouseButtonPressed(LeftButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseDown(MousePosition, 0);
			}
		}
	}
	if (InputManager.IsMouseButtonPressed(RightButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseDown(MousePosition, 0);
			}
		}
	}
	if (InputManager.IsMouseButtonPressed(RightButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseDown(MousePosition, 1);
			}
		}
	}
	if (InputManager.IsMouseButtonReleased(RightButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseUp(MousePosition, 1);
			}
		}
	}
}

void UWorld::ProcessViewportInput()
{
	const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();

	if (InputManager.IsMouseButtonPressed(LeftButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseDown(MousePosition, 0);
			}
		}
	}
	if (InputManager.IsMouseButtonPressed(RightButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseDown(MousePosition, 1);
			}
		}
	}
	if (InputManager.IsMouseButtonReleased(LeftButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseUp(MousePosition, 0);
			}
		}
	}
	if (InputManager.IsMouseButtonReleased(RightButton))
	{
		const FVector2D MousePosition = UInputManager::GetInstance().GetMousePosition();
		{
			if (MultiViewport)
			{
				MultiViewport->OnMouseUp(MousePosition, 1);
			}
		}
	}
	MultiViewport->OnMouseMove(MousePosition);
}

void UWorld::LoadScene(const FString& SceneName)
{
	namespace fs = std::filesystem;
	fs::path path = fs::path("Scene") / SceneName;
	if (path.extension().string() != ".Scene")
	{
		path.replace_extension(".Scene");
	}

	const FString FilePath = path.make_preferred().string();

	// [1] 로드 시작 전 현재 카운터 백업
	const uint32 PreLoadNext = UObject::PeekNextUUID();

	// [2] 파일 NextUUID는 현재보다 클 때만 반영(절대 하향 설정 금지)
	uint32 LoadedNextUUID = 0;
	if (FSceneLoader::TryReadNextUUID(FilePath, LoadedNextUUID))
	{
		if (LoadedNextUUID > UObject::PeekNextUUID())
		{
			UObject::SetNextUUID(LoadedNextUUID);
		}
	}

	// [3] 기존 씬 비우기
	CreateNewScene();

	// [4] 로드
	FPerspectiveCameraData CamData{};
	const TArray<FPrimitiveData>& Primitives = FSceneLoader::Load(FilePath, &CamData);

	// 마우스 델타 초기화
	const FVector2D CurrentMousePos = UInputManager::GetInstance().GetMousePosition();
	UInputManager::GetInstance().SetLastMousePosition(CurrentMousePos);

	// 카메라 적용
	if (MainCameraActor && MainCameraActor->GetCameraComponent())
	{
		UCameraComponent* Cam = MainCameraActor->GetCameraComponent();

		// 위치/회전(월드 트랜스폼)
		MainCameraActor->SetActorLocation(CamData.Location);
		MainCameraActor->SetActorRotation(FQuat::MakeFromEuler(CamData.Rotation));

		// 입력 경로와 동일한 방식으로 각도/회전 적용
		// 매핑: Pitch = CamData.Rotation.Y, Yaw = CamData.Rotation.Z
		MainCameraActor->SetAnglesImmediate(CamData.Rotation.Y, CamData.Rotation.Z);

		// UIManager의 카메라 회전 상태도 동기화
		UIManager.UpdateMouseRotation(CamData.Rotation.Y, CamData.Rotation.Z);

		// 프로젝션 파라미터
		Cam->SetFOV(CamData.FOV);
		Cam->SetClipPlanes(CamData.NearClip, CamData.FarClip);

		// UI 위젯에 현재 카메라 상태로 재동기화 요청
		UIManager.SyncCameraControlFromCamera();
	}

	// 1) 현재 월드에서 이미 사용 중인 UUID 수집(엔진 액터 + 기즈모)
	std::unordered_set<uint32> UsedUUIDs;
	auto AddUUID = [&](AActor* A) { if (A) UsedUUIDs.insert(A->UUID); };
	for (AActor* Eng : EngineActors)
	{
		AddUUID(Eng);
	}
	AddUUID(GizmoActor); // Gizmo는 EngineActors에 안 들어갈 수 있으므로 명시 추가

	uint32 MaxAssignedUUID = 0;

	for (const FPrimitiveData& Primitive : Primitives)
	{
		// 스폰 시 필요한 초기 트랜스폼은 그대로 넘김
		AStaticMeshActor* StaticMeshActor = SpawnActor<AStaticMeshActor>(
			FTransform(Primitive.Location,
				SceneRotUtil::QuatFromEulerZYX_Deg(Primitive.Rotation),
				Primitive.Scale));

		// 스폰 시점에 자동 발급된 고유 UUID (충돌 시 폴백으로 사용)
		uint32 Assigned = StaticMeshActor->UUID;

		// 우선 스폰된 UUID를 사용 중으로 등록
		UsedUUIDs.insert(Assigned);

		// 2) 파일의 UUID를 우선 적용하되, 충돌이면 스폰된 UUID 유지
		if (Primitive.UUID != 0)
		{
			if (UsedUUIDs.find(Primitive.UUID) == UsedUUIDs.end())
			{
				// 스폰된 ID 등록 해제 후 교체
				UsedUUIDs.erase(Assigned);
				StaticMeshActor->UUID = Primitive.UUID;
				Assigned = Primitive.UUID;
				UsedUUIDs.insert(Assigned);
			}
			else
			{
				// 충돌: 파일 UUID 사용 불가 → 경고 로그 및 스폰된 고유 UUID 유지
				UE_LOG("LoadScene: UUID collision detected (%u). Keeping generated %u for actor.",
					Primitive.UUID, Assigned);
			}
		}

		MaxAssignedUUID = std::max(MaxAssignedUUID, Assigned);

		if (UStaticMeshComponent* SMC = StaticMeshActor->GetStaticMeshComponent())
		{
			FPrimitiveData Temp = Primitive;
			SMC->Serialize(true, Temp);

			FString LoadedAssetPath;
			if (UStaticMesh* Mesh = SMC->GetStaticMesh())
			{
				LoadedAssetPath = Mesh->GetAssetPathFileName();
			}

			if (LoadedAssetPath == "Data/Sphere.obj")
			{
				StaticMeshActor->SetCollisionComponent(EPrimitiveType::Sphere);
			}
			else
			{
				StaticMeshActor->SetCollisionComponent();
			}

			FString BaseName = "StaticMesh";
			if (!LoadedAssetPath.empty())
			{
				BaseName = RemoveObjExtension(LoadedAssetPath);
			}
			StaticMeshActor->SetName(GenerateUniqueActorName(BaseName));
		}
	}



	// 3) 최종 보정: 전역 카운터는 절대 하향 금지 + 현재 사용된 최대값 이후로 설정
	const uint32 DuringLoadNext = UObject::PeekNextUUID();
	const uint32 SafeNext = std::max({ DuringLoadNext, MaxAssignedUUID + 1, PreLoadNext });
	UObject::SetNextUUID(SafeNext);



	if (Level)
	{
		InitializeSceneGraph(Level->GetActors());
	}
}

void UWorld::SaveScene(const FString& SceneName)
{
	TArray<FPrimitiveData> Primitives;

	for (AActor* Actor : Level->GetActors())
	{
		if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			FPrimitiveData Data;
			Data.UUID = Actor->UUID;
			Data.Type = "StaticMeshComp";
			if (UStaticMeshComponent* SMC = MeshActor->GetStaticMeshComponent())
			{
				SMC->Serialize(false, Data); // 여기서 RotUtil 적용됨(상위 Serialize)
			}
			Primitives.push_back(Data);
		}
		else
		{
			FPrimitiveData Data;
			Data.UUID = Actor->UUID;
			Data.Type = "Actor";

			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
			{
				Prim->Serialize(false, Data); // 여기서 RotUtil 적용됨
			}
			else
			{
				// 루트가 Primitive가 아닐 때도 동일 규칙으로 저장
				Data.Location = Actor->GetActorLocation();
				Data.Rotation = SceneRotUtil::EulerZYX_Deg_FromQuat(Actor->GetActorRotation());
				Data.Scale = Actor->GetActorScale();
			}

			Data.ObjStaticMeshAsset.clear();
			Primitives.push_back(Data);
		}
	}

	// 카메라 데이터 채우기
	const FPerspectiveCameraData* CamPtr = nullptr;
	FPerspectiveCameraData CamData;
	if (MainCameraActor && MainCameraActor->GetCameraComponent())
	{
		UCameraComponent* Cam = MainCameraActor->GetCameraComponent();

		CamData.Location = MainCameraActor->GetActorLocation();

		// 내부 누적 각도로 저장: Pitch=Y, Yaw=Z, Roll=0
		CamData.Rotation.X = 0.0f;
		CamData.Rotation.Y = MainCameraActor->GetCameraPitch();
		CamData.Rotation.Z = MainCameraActor->GetCameraYaw();

		CamData.FOV = Cam->GetFOV();
		CamData.NearClip = Cam->GetNearClip();
		CamData.FarClip = Cam->GetFarClip();
		CamPtr = &CamData;
	}

	// Scene 디렉터리에 저장
	FSceneLoader::Save(Primitives, CamPtr, SceneName);
}

void UWorld::SaveSceneV2(const FString& SceneName)
{
	FSceneData SceneData;
	SceneData.Version = 2;
	SceneData.NextUUID = UObject::PeekNextUUID();

	// 카메라 데이터 채우기
	if (MainCameraActor && MainCameraActor->GetCameraComponent())
	{
		UCameraComponent* Cam = MainCameraActor->GetCameraComponent();
		SceneData.Camera.Location = MainCameraActor->GetActorLocation();
		SceneData.Camera.Rotation.X = 0.0f;
		SceneData.Camera.Rotation.Y = MainCameraActor->GetCameraPitch();
		SceneData.Camera.Rotation.Z = MainCameraActor->GetCameraYaw();
		SceneData.Camera.FOV = Cam->GetFOV();
		SceneData.Camera.NearClip = Cam->GetNearClip();
		SceneData.Camera.FarClip = Cam->GetFarClip();
	}

	// Actor 및 Component 계층 수집
	for (AActor* Actor : Level->GetActors())
	{
		if (!Actor) continue;

		// Actor 데이터
		FActorData ActorData;
		ActorData.UUID = Actor->UUID;
		ActorData.Name = Actor->GetName().ToString();
		ActorData.Type = Actor->GetClass()->Name;

		if (Actor->GetRootComponent())
			ActorData.RootComponentUUID = Actor->GetRootComponent()->UUID;

		SceneData.Actors.push_back(ActorData);

		// OwnedComponents 순회 (모든 컴포넌트 포함)
		for (UActorComponent* ActorComp : Actor->GetComponents())
		{
			if (!ActorComp) continue;

			// SceneComponent만 처리 (Transform 정보가 있는 컴포넌트)
			USceneComponent* Comp = Cast<USceneComponent>(ActorComp);
			if (!Comp) continue;

			FComponentData CompData;
			CompData.UUID = Comp->UUID;
			CompData.OwnerActorUUID = Actor->UUID;

			// 부모 컴포넌트 UUID (RootComponent면 0)
			if (Comp->GetAttachParent())
				CompData.ParentComponentUUID = Comp->GetAttachParent()->UUID;
			else
				CompData.ParentComponentUUID = 0;

			// Transform
			CompData.RelativeLocation = Comp->GetRelativeLocation();
			CompData.RelativeRotation = Comp->GetRelativeRotation().ToEuler();
			CompData.RelativeScale = Comp->GetRelativeScale();

			// Type 자동 가져오기
			CompData.Type = Comp->GetClass()->Name;

			// Type별 속성
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Comp))
			{
				if (StaticMeshComponent->GetStaticMesh())
				{
					CompData.StaticMesh = StaticMeshComponent->GetStaticMesh()->GetAssetPathFileName();
					UE_LOG("SaveScene: StaticMesh saved: %s", CompData.StaticMesh.c_str());
				}
				else
				{
					UE_LOG("SaveScene: StaticMeshComponent has no StaticMesh assigned");
				}
				// TODO: Materials 수집
			}

			SceneData.Components.push_back(CompData);
		}
	}

	// Scene 디렉터리에 V2 포맷으로 저장
	FSceneLoader::SaveV2(SceneData, SceneName);
}

void UWorld::LoadSceneV2(const FString& SceneName)
{
	namespace fs = std::filesystem;
	fs::path path = fs::path("Scene") / SceneName;
	if (path.extension().string() != ".Scene")
	{
		path.replace_extension(".Scene");
	}

	const FString FilePath = path.make_preferred().string();

	// NextUUID 업데이트
	uint32 LoadedNextUUID = 0;
	if (FSceneLoader::TryReadNextUUID(FilePath, LoadedNextUUID))
	{
		if (LoadedNextUUID > UObject::PeekNextUUID())
		{
			UObject::SetNextUUID(LoadedNextUUID);
		}
	}

	// 기존 씬 비우기
	CreateNewScene();

	// V2 데이터 로드
	FSceneData SceneData = FSceneLoader::LoadV2(FilePath);

	// 마우스 델타 초기화
	const FVector2D CurrentMousePos = UInputManager::GetInstance().GetMousePosition();
	UInputManager::GetInstance().SetLastMousePosition(CurrentMousePos);



	if (MainCameraActor && MainCameraActor->GetCameraComponent())
	{
		UCameraComponent* Cam = MainCameraActor->GetCameraComponent();
		MainCameraActor->SetActorLocation(SceneData.Camera.Location);
		MainCameraActor->SetCameraPitch(SceneData.Camera.Rotation.Y);
		MainCameraActor->SetCameraYaw(SceneData.Camera.Rotation.Z);

		// 입력 경로와 동일한 방식으로 각도/회전 적용
	  // 매핑: Pitch = CamData.Rotation.Y, Yaw = CamData.Rotation.Z
		MainCameraActor->SetAnglesImmediate(SceneData.Camera.Rotation.Y, SceneData.Camera.Rotation.Z);

		// UIManager의 카메라 회전 상태도 동기화
		UIManager.UpdateMouseRotation(SceneData.Camera.Rotation.Y, SceneData.Camera.Rotation.Z);

		Cam->SetFOV(SceneData.Camera.FOV);
		Cam->SetClipPlanes(SceneData.Camera.NearClip, SceneData.Camera.FarClip);

		// UI 위젯에 현재 카메라 상태로 재동기화 요청
		UIManager.SyncCameraControlFromCamera();

	}

	// UUID → Object 매핑 테이블
	TMap<uint32, AActor*> ActorMap;
	TMap<uint32, USceneComponent*> ComponentMap;

	// ========================================
	// Pass 1: Actor 및 Component 생성
	// ========================================
	for (const FActorData& ActorData : SceneData.Actors)
	{
		AActor* NewActor = Cast<AActor>(NewObject(ActorData.Type));

		if (!NewActor)
		{
			UE_LOG("Failed to create Actor: %s", ActorData.Type.c_str());
			continue;
		}

		NewActor->UUID = ActorData.UUID;
		NewActor->SetName(ActorData.Name);
		NewActor->SetWorld(this);

		ActorMap.Add(ActorData.UUID, NewActor);
	}

	// Component 생성
	for (const FComponentData& CompData : SceneData.Components)
	{
		USceneComponent* NewComp = Cast<USceneComponent>(NewObject(CompData.Type));

		if (!NewComp)
		{
			UE_LOG("Failed to create Component: %s", CompData.Type.c_str());
			continue;
		}

		NewComp->UUID = CompData.UUID;
		NewComp->SetRelativeLocation(CompData.RelativeLocation);
		NewComp->SetRelativeRotation(FQuat::MakeFromEuler(CompData.RelativeRotation));
		NewComp->SetRelativeScale(CompData.RelativeScale);

		// Type별 속성 복원
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(NewComp))
		{
			if (!CompData.StaticMesh.empty())
			{
				SMC->SetStaticMesh(CompData.StaticMesh);
			}
			// TODO: Materials 복원
		}

		// Owner Actor 설정
		if (AActor** OwnerActor = ActorMap.Find(CompData.OwnerActorUUID))
		{
			NewComp->SetOwner(*OwnerActor);
		}

		ComponentMap.Add(CompData.UUID, NewComp);
	}

	// ========================================
	// Pass 2: Actor-Component 연결 및 계층 구조 설정
	// ========================================
	for (const FActorData& ActorData : SceneData.Actors)
	{
		AActor** ActorPtr = ActorMap.Find(ActorData.UUID);
		if (!ActorPtr) continue;

		AActor* Actor = *ActorPtr;

		// RootComponent 설정
		if (USceneComponent** RootCompPtr = ComponentMap.Find(ActorData.RootComponentUUID))
		{
			Actor->RootComponent = *RootCompPtr;
		}
	}

	// Component 부모-자식 관계 설정
	for (const FComponentData& CompData : SceneData.Components)
	{
		USceneComponent** CompPtr = ComponentMap.Find(CompData.UUID);
		if (!CompPtr) continue;

		USceneComponent* Comp = *CompPtr;

		// 부모 컴포넌트 연결 (ParentUUID가 0이 아니면)
		if (CompData.ParentComponentUUID != 0)
		{
			if (USceneComponent** ParentPtr = ComponentMap.Find(CompData.ParentComponentUUID))
			{
				Comp->SetupAttachment(*ParentPtr, EAttachmentRule::KeepRelative);
			}
		}

		// Actor의 OwnedComponents에 추가
		if (AActor** OwnerActorPtr = ActorMap.Find(CompData.OwnerActorUUID))
		{
			(*OwnerActorPtr)->OwnedComponents.Add(Comp);
		}
	}

	// Actor를 Level에 추가
	for (auto& Pair : ActorMap)
	{
		AActor* Actor = Pair.second;
		Level->AddActor(Actor);

		// StaticMeshActor 전용 포인터 재설정
		if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
		{
			StaticMeshActor->SetStaticMeshComponent(Cast<UStaticMeshComponent>(StaticMeshActor->RootComponent));

			// CollisionComponent 찾기
			for (UActorComponent* Comp : StaticMeshActor->OwnedComponents)
			{
				if (UAABoundingBoxComponent* BBoxComp = Cast<UAABoundingBoxComponent>(Comp))
				{
					StaticMeshActor->CollisionComponent = BBoxComp;
					StaticMeshActor->SetCollisionComponent(EPrimitiveType::Sphere);
					break;
				}
			}
		}
	}

	// NextUUID 업데이트 (로드된 모든 UUID + 1)
	uint32 MaxUUID = SceneData.NextUUID;
	if (MaxUUID > UObject::PeekNextUUID())
	{
		UObject::SetNextUUID(MaxUUID);
	}

	UE_LOG("Scene V2 loaded successfully: %s", SceneName.c_str());
}

AGizmoActor* UWorld::GetGizmoActor()
{
	return GizmoActor;
}

UWorld* UWorld::DuplicateWorldForPIE(UWorld* EditorWorld)
{
	if (!EditorWorld)
	{
		return nullptr;
	}

	// 새로운 PIE 월드 생성
	UWorld* PIEWorld = NewObject<UWorld>();
	if (!PIEWorld)
	{
		return nullptr;
	}
	PIEWorld->Renderer = EditorWorld->Renderer;
	PIEWorld->MainViewport = EditorWorld->MainViewport;
	PIEWorld->MultiViewport = EditorWorld->MultiViewport;
	// WorldType을 PIE로 설정
	PIEWorld->WorldType = (EWorldType::PIE);

	//// Renderer 공유 (얕은 복사)
	//PIEWorld->Renderer = EditorWorld->Renderer;

	// MainCameraActor 공유 (PIE는 일단 Editor 카메라 사용)
	PIEWorld->MainCameraActor = EditorWorld->MainCameraActor;

	// GizmoActor는 PIE에서 사용하지 않음
	PIEWorld->GizmoActor = nullptr;

	// GridActor 공유 (선택적)
	PIEWorld->GridActor = nullptr;

	// Level 복제
	if (EditorWorld->GetLevel())
	{
		ULevel* EditorLevel = EditorWorld->GetLevel();
		ULevel* PIELevel = PIEWorld->GetLevel();

		if (PIELevel)
		{
			// Level의 Actors를 복제
			for (AActor* EditorActor : EditorLevel->GetActors())
			{
				if (EditorActor)
				{
					AActor* PIEActor = Cast<AActor>(EditorActor->Duplicate());//체크!

					if (PIEActor)
					{
						PIELevel->AddActor(PIEActor);
						PIEActor->SetWorld(PIEWorld);
					}
				}
			}

			PIEWorld->Level = PIELevel;
		}
	}
	return PIEWorld;
}

void UWorld::InitializeActorsForPlay()
{
	// 모든 액터의 BeginPlay 호출
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor)
			{
				Actor->BeginPlay();
			}
		}
	}
}

void UWorld::CleanupWorld()
{
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor)
			{
				Actor->EndPlay(EEndPlayReason::Quit);
			}
		}
	}
}

/**
 * @brief 이미 생성한 Actor를 spawn하기 위한 shortcut 함수
 * @param InActor World에 생성할 Actor
 */
void UWorld::SpawnActor(AActor* InActor)
{
	InActor->SetWorld(this);


	if (UStaticMeshComponent* ActorComp = Cast<UStaticMeshComponent>(InActor->RootComponent))
	{
		FString ActorName = GenerateUniqueActorName(
			GetBaseNameNoExt(ActorComp->GetStaticMesh()->GetAssetPathFileName())
		);
		InActor->SetName(ActorName);
	}

	Level->GetActors().Add(InActor);
}
