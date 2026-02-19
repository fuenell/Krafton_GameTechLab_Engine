#include <windows.h>

// D3D 사용에 필요한 라이브러리들을 링크합니다.
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

// D3D 사용에 필요한 헤더파일들을 포함합니다.
#include <d3d11.h>
#include <d3dcompiler.h>

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "imGui/imgui_impl_win32.h"

#include <string>

// 1. Define the triangle vertices
struct FVertexSimple
{
	float x, y, z;    // Position
	float r, g, b, a; // Color
};

// Structure for a 3D vector
struct FVector
{
	float x, y, z;
	FVector(float _x = 0, float _y = 0, float _z = 0) : x(_x), y(_y), z(_z) {}

	// 벡터의 크기(길이)의 제곱을 반환합니다.
	float LengthSquared() const
	{
		return x * x + y * y + z * z;
	}

	// 벡터의 크기(길이)를 반환합니다.
	float Length() const
	{
		return sqrt(LengthSquared());
	}

	// 벡터를 정규화합니다 (크기를 1로 만듭니다).
	void Normalize()
	{
		float length = Length();
		if (length > 0)
		{
			x /= length;
			y /= length;
			z /= length;
		}
	}

	// FVector 연산자 오버로딩
	FVector operator+(const FVector& other) const
	{
		return FVector(x + other.x, y + other.y, z + other.z);
	}
	FVector operator-(const FVector& other) const
	{
		return FVector(x - other.x, y - other.y, z - other.z);
	}
	FVector operator*(float scalar) const
	{
		return FVector(x * scalar, y * scalar, z * scalar);
	}
	FVector& operator+=(const FVector& other)
	{
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}
	FVector& operator-=(const FVector& other)
	{
		x -= other.x;
		y -= other.y;
		z -= other.z;
		return *this;
	}
};

// 두 벡터의 내적(Dot Product)을 계산하는 전역 함수
inline float DotProduct(const FVector& a, const FVector& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

#include "Sphere.h"

class URenderer
{
public:
	// URenderer Class에 아래 함수를 추가 하세요.
	void Prepare()
	{
		DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);

		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		DeviceContext->RSSetViewports(1, &ViewportInfo);
		DeviceContext->RSSetState(RasterizerState);

		DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, nullptr);
		DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}

	void PrepareShader()
	{
		DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
		DeviceContext->PSSetShader(SimplePixelShader, nullptr, 0);
		DeviceContext->IASetInputLayout(SimpleInputLayout);

		//여기에 추가하세요.
		// 버텍스 쉐이더에 상수 버퍼를 설정합니다.
		if (ConstantBuffer)
		{
			DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
		}
	}

	void RenderPrimitive(ID3D11Buffer* pBuffer, UINT numVertices)
	{
		UINT offset = 0;
		DeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &Stride, &offset);

		DeviceContext->Draw(numVertices, 0);
	}
	// 기존 URenderer Class에 아래 코드를 추가 하세요.

	ID3D11VertexShader* SimpleVertexShader;
	ID3D11PixelShader* SimplePixelShader;
	ID3D11InputLayout* SimpleInputLayout;
	unsigned int Stride;

	void CreateShader()
	{
		ID3DBlob* vertexshaderCSO;
		ID3DBlob* pixelshaderCSO;

		D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);

		Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, &SimpleVertexShader);

		D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", 0, 0, &pixelshaderCSO, nullptr);

		Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(), pixelshaderCSO->GetBufferSize(), nullptr, &SimplePixelShader);

		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		Device->CreateInputLayout(layout, ARRAYSIZE(layout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), &SimpleInputLayout);

		Stride = sizeof(FVertexSimple);

		vertexshaderCSO->Release();
		pixelshaderCSO->Release();
	}

	void ReleaseShader()
	{
		if (SimpleInputLayout)
		{
			SimpleInputLayout->Release();
			SimpleInputLayout = nullptr;
		}

		if (SimplePixelShader)
		{
			SimplePixelShader->Release();
			SimplePixelShader = nullptr;
		}

		if (SimpleVertexShader)
		{
			SimpleVertexShader->Release();
			SimpleVertexShader = nullptr;
		}
	}

	// Direct3D 11 장치(Device)와 장치 컨텍스트(Device Context) 및 스왑 체인(Swap Chain)을 관리하기 위한 포인터들
	ID3D11Device* Device = nullptr; // GPU와 통신하기 위한 Direct3D 장치
	ID3D11DeviceContext* DeviceContext = nullptr; // GPU 명령 실행을 담당하는 컨텍스트
	IDXGISwapChain* SwapChain = nullptr; // 프레임 버퍼를 교체하는 데 사용되는 스왑 체인

	// 렌더링에 필요한 리소스 및 상태를 관리하기 위한 변수들
	ID3D11Texture2D* FrameBuffer = nullptr; // 화면 출력용 텍스처
	ID3D11RenderTargetView* FrameBufferRTV = nullptr; // 텍스처를 렌더 타겟으로 사용하는 뷰
	ID3D11RasterizerState* RasterizerState = nullptr; // 래스터라이저 상태(컬링, 채우기 모드 등 정의)
	ID3D11Buffer* ConstantBuffer = nullptr; // 쉐이더에 데이터를 전달하기 위한 상수 버퍼

	FLOAT ClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f }; // 화면을 초기화(clear)할 때 사용할 색상 (RGBA)
	D3D11_VIEWPORT ViewportInfo; // 렌더링 영역을 정의하는 뷰포트 정보

	// 렌더러 초기화 함수
	void Create(HWND hWindow)
	{
		// Direct3D 장치 및 스왑 체인 생성
		CreateDeviceAndSwapChain(hWindow);

		// 프레임 버퍼 생성
		CreateFrameBuffer();

		// 래스터라이저 상태 생성
		CreateRasterizerState();

		// 깊이 스텐실 버퍼 및 블렌드 상태는 이 코드에서는 다루지 않음
	}

	// Direct3D 장치 및 스왑 체인을 생성하는 함수
	void CreateDeviceAndSwapChain(HWND hWindow)
	{
		// 지원하는 Direct3D 기능 레벨을 정의
		D3D_FEATURE_LEVEL featurelevels[] = { D3D_FEATURE_LEVEL_11_0 };

		// 스왑 체인 설정 구조체 초기화
		DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
		swapchaindesc.BufferDesc.Width = 0; // 창 크기에 맞게 자동으로 설정
		swapchaindesc.BufferDesc.Height = 0; // 창 크기에 맞게 자동으로 설정
		swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 색상 포맷
		swapchaindesc.SampleDesc.Count = 1; // 멀티 샘플링 비활성화
		swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 렌더 타겟으로 사용
		swapchaindesc.BufferCount = 2; // 더블 버퍼링
		swapchaindesc.OutputWindow = hWindow; // 렌더링할 창 핸들
		swapchaindesc.Windowed = TRUE; // 창 모드
		swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 스왑 방식

		// Direct3D 장치와 스왑 체인을 생성
		D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
			featurelevels, ARRAYSIZE(featurelevels), D3D11_SDK_VERSION,
			&swapchaindesc, &SwapChain, &Device, nullptr, &DeviceContext);

		// 생성된 스왑 체인의 정보 가져오기
		SwapChain->GetDesc(&swapchaindesc);

		// 뷰포트 정보 설정
		ViewportInfo = { 0.0f, 0.0f, (float)swapchaindesc.BufferDesc.Width, (float)swapchaindesc.BufferDesc.Height, 0.0f, 1.0f };
	}

	// Direct3D 장치 및 스왑 체인을 해제하는 함수
	void ReleaseDeviceAndSwapChain()
	{
		if (DeviceContext)
		{
			DeviceContext->Flush(); // 남아있는 GPU 명령 실행
		}

		if (SwapChain)
		{
			SwapChain->Release();
			SwapChain = nullptr;
		}

		if (Device)
		{
			Device->Release();
			Device = nullptr;
		}

		if (DeviceContext)
		{
			DeviceContext->Release();
			DeviceContext = nullptr;
		}
	}

	// 프레임 버퍼를 생성하는 함수
	void CreateFrameBuffer()
	{
		// 스왑 체인으로부터 백 버퍼 텍스처 가져오기
		SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

		// 렌더 타겟 뷰 생성
		D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
		framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; // 색상 포맷
		framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; // 2D 텍스처

		Device->CreateRenderTargetView(FrameBuffer, &framebufferRTVdesc, &FrameBufferRTV);
	}

	// 프레임 버퍼를 해제하는 함수
	void ReleaseFrameBuffer()
	{
		if (FrameBuffer)
		{
			FrameBuffer->Release();
			FrameBuffer = nullptr;
		}

		if (FrameBufferRTV)
		{
			FrameBufferRTV->Release();
			FrameBufferRTV = nullptr;
		}
	}

	// 래스터라이저 상태를 생성하는 함수
	void CreateRasterizerState()
	{
		D3D11_RASTERIZER_DESC rasterizerdesc = {};
		rasterizerdesc.FillMode = D3D11_FILL_SOLID; // 채우기 모드
		rasterizerdesc.CullMode = D3D11_CULL_BACK; // 백 페이스 컬링

		Device->CreateRasterizerState(&rasterizerdesc, &RasterizerState);
	}

	// 래스터라이저 상태를 해제하는 함수
	void ReleaseRasterizerState()
	{
		if (RasterizerState)
		{
			RasterizerState->Release();
			RasterizerState = nullptr;
		}
	}

	// 렌더러에 사용된 모든 리소스를 해제하는 함수
	void Release()
	{
		RasterizerState->Release();

		// 렌더 타겟을 초기화
		DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

		ReleaseFrameBuffer();
		ReleaseDeviceAndSwapChain();
	}

	// 스왑 체인의 백 버퍼와 프론트 버퍼를 교체하여 화면에 출력
	void SwapBuffer()
	{
		SwapChain->Present(1, 0); // 1: VSync 활성화
	}

	ID3D11Buffer* CreateVertexBuffer(FVertexSimple* vertices, UINT byteWidth)
	{
		// 2. Create a vertex buffer
		D3D11_BUFFER_DESC vertexbufferdesc = {};
		vertexbufferdesc.ByteWidth = byteWidth;
		vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE; // will never be updated 
		vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA vertexbufferSRD = { vertices };

		ID3D11Buffer* vertexBuffer;

		Device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, &vertexBuffer);

		return vertexBuffer;
	}

	void ReleaseVertexBuffer(ID3D11Buffer* vertexBuffer)
	{
		vertexBuffer->Release();
	}

	struct FConstants
	{
		FVector Offset;
		float Scale;    // 공 크기 (스케일)
	};

	void CreateConstantBuffer()
	{
		D3D11_BUFFER_DESC constantbufferdesc = {};
		constantbufferdesc.ByteWidth = sizeof(FConstants) + 0xf & 0xfffffff0; // ensure constant buffer size is multiple of 16 bytes
		constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC; // will be updated from CPU every frame
		constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		Device->CreateBuffer(&constantbufferdesc, nullptr, &ConstantBuffer);
	}

	void ReleaseConstantBuffer()
	{
		if (ConstantBuffer)
		{
			ConstantBuffer->Release();
			ConstantBuffer = nullptr;
		}
	}

	void UpdateConstant(FVector Offset, float scale)
	{
		if (ConstantBuffer)
		{
			D3D11_MAPPED_SUBRESOURCE constantbufferMSR;

			DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
			FConstants* constants = (FConstants*)constantbufferMSR.pData;
			{
				constants->Offset = Offset;
				constants->Scale = scale;
			}
			DeviceContext->Unmap(ConstantBuffer, 0);
		}
	}
};

const float leftBorder = -1.0f;
const float rightBorder = 1.0f;
const float topBorder = 1.0f;
const float bottomBorder = -1.0f;
const float sphereRadius = 1.0f;
const FVector G(0.0f, -9.8f, 0.0f); // 중력 가속도 (Y축을 아래 방향으로 설정)

float scaleMod = 0.1f;

bool bUseGravity = true;
bool bUseTrashCan = false;

// 클래스 이름 외에는 자유롭게 수정하세요.
class UPrimitive
{
public:
	virtual ~UPrimitive() {}
	virtual void Move(float t) = 0;
	virtual void Draw(URenderer& renderer) = 0;
	virtual bool CheckCollision(UPrimitive* other) = 0;
	virtual void ResolveCollision(UPrimitive* other) = 0;
};

class UBall : public UPrimitive
{
public:
	// 클래스 이름과, 아래 다섯개의 변수 이름은 변경하지 않습니다.
	FVector Location;
	FVector Velocity;
	float Radius;
	float Mass;
	static int TotalNumBalls;

	// 렌더링에 필요한 데이터
	ID3D11Buffer* VertexBuffer;
	UINT NumVertices;

	void Move(float t) override
	{
		// WinMain에서 받은 t는 밀리초(ms)이므로 초(s) 단위로 변환합니다.
		float deltaTime = t / 1000.0f;

		float restitution = 0.98f; // 충돌 계수 (0~1)

		// 중력 적용 여부를 확인합니다.
		if (bUseGravity)
		{
			// 중력 가속도를 속도에 더해줍니다.
			Velocity.y += G.y * deltaTime;
		}

		// 속도를 공위치에 더해 공을 실질적으로 움직임
		Location.x += Velocity.x * deltaTime;
		Location.y += Velocity.y * deltaTime;
		Location.z += Velocity.z * deltaTime;

		// 벽과 충돌 여부를 체크하고 충돌시 속도에 음수를 곱해 방향을 바꿈
		float renderRadius = Radius * scaleMod;
		if (Location.x < leftBorder + renderRadius)
		{
			Velocity.x *= -restitution;
		}
		if (Location.x > rightBorder - renderRadius)
		{
			Velocity.x *= -restitution;
		}
		if (Location.y < bottomBorder + renderRadius)
		{
			Velocity.y *= -restitution;
		}
		if (Location.y > topBorder - renderRadius)
		{
			Velocity.y *= -restitution;
		}

		// 맵 이탈 방지
		if (Location.x < leftBorder + renderRadius)
		{
			Location.x = leftBorder + renderRadius;
		}
		if (Location.x > rightBorder - renderRadius)
		{
			Location.x = rightBorder - renderRadius;
		}
		if (Location.y < bottomBorder + renderRadius)
		{
			Location.y = bottomBorder + renderRadius;
		}
		if (Location.y > topBorder - renderRadius)
		{
			Location.y = topBorder - renderRadius;
		}
	}

	void Draw(URenderer& renderer) override
	{
		// 자신의 위치와 크기 정보를 렌더러에 전달합니다.
		renderer.UpdateConstant(Location, Radius);
		// 자신의 버텍스 버퍼를 사용하여 렌더링을 요청합니다.
		renderer.RenderPrimitive(VertexBuffer, NumVertices);
	}

	bool CheckCollision(UPrimitive* other) override
	{
		UBall* otherBall = dynamic_cast<UBall*>(other);
		if (!otherBall)
		{
			return false;
		}

		// 두 공의 중심 사이의 거리 벡터
		FVector distVec = Location - otherBall->Location;
		float distSquared = distVec.x * distVec.x + distVec.y * distVec.y + distVec.z * distVec.z;

		// 두 공의 반지름 합
		float radiusSum = (Radius + otherBall->Radius) * scaleMod;
		float radiusSumSquared = radiusSum * radiusSum;

		// 중심 사이의 거리가 반지름의 합보다 작으면 충돌
		bool isCollision = distSquared < radiusSumSquared;

		return isCollision;
	}

	void ResolveCollision(UPrimitive* other) override
	{
		UBall* otherBall = dynamic_cast<UBall*>(other);
		if (!otherBall)
		{
			return;
		}

		// B가 A에 충돌한 방향
		FVector normal = Location - otherBall->Location;
		float distance = normal.Length();
		normal.Normalize();

		// A가 B에 가까워지는 상대 속도
		FVector relativeVelocity = Velocity - otherBall->Velocity;

		// 상대 속도와 충돌한 방향을 내적해서 가까워지는 중인지 확인
		// 음수면 서로 반대 방향이라는 뜻 = 가까워지는 중
		float velAlongNormal = DotProduct(relativeVelocity, normal);

		// 두 공이 서로 멀어지고 있다면 충돌 처리를 하지 않음
		if (velAlongNormal > 0)
		{
			return;
		}

		// 탄성 충돌 공식으로 충격량을 구함
		float restitution = 1.0f; // 완전 탄성 충돌 계수
		float j = -(1.0f + restitution) * velAlongNormal;
		j /= (1.0f / Mass + 1.0f / otherBall->Mass);

		// 충격량의 출동 백터를 곱해 충격량 벡터를 한쪽에 더하고 반대쪽에서 뺀다
		FVector impulse = normal * j;
		Velocity += (impulse * (1.0f / Mass));
		otherBall->Velocity -= (impulse * (1.0f / otherBall->Mass));

		// 겹침 현상 해결
		float penetration = ((Radius + otherBall->Radius) * scaleMod) - distance;
		const float slop = 0.01f;   // 아주 작은 겹침은 무시하여 떨림 방지
		if (slop < penetration)
		{
			// 보정 퍼센트
			float percent = 0.2f;

			// (겹쳐진 양의 절반 * 보정량) 만큼 서로 반대 방향으로 위치 시킨다
			FVector correction = normal * penetration * 0.5f * percent;
			Location += correction;
			otherBall->Location -= correction;
		}
	}
};

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 각종 메시지를 처리할 함수
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	switch (message)
	{
	case WM_DESTROY:
		// Signal that the app should quit
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

// 숫자 필터 콜백 (숫자 외 문자 입력 차단)
int FilterDigits(ImGuiInputTextCallbackData* data)
{
	if (data->EventChar < '0' || data->EventChar > '9')
	{
		return 1; // 입력 무시
	}
	return 0;
}

// 반드시 UBall이 아닌 UPrimitive로 선언하여야 하며 바꾸면 안됩니다.
UPrimitive** PrimitiveList = nullptr;
int UBall::TotalNumBalls = 0;

// 임의의 위치, 속도, 크기를 가진 공 생성
UBall* CreateRandomBall(ID3D11Buffer* vertexBuffer, UINT numVertices)
{
	UBall* ball = new UBall();

	const float ballSpeed = 0.05f;

	// 도형의 움직임 정도를 담을 offset 변수를 Main 루프 바로 앞에 정의 하세요.	
	FVector	offset(0.0f);
	FVector	velocity(0.0f);

	velocity.x = ((float)(rand() % 100 - 50)) * ballSpeed;
	velocity.y = ((float)(rand() % 100 - 50)) * ballSpeed;

	// 임의 크기 지정
	float maxRadius = 1.0f;
	float minRadius = 0.5f;
	float radius = minRadius + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (maxRadius - minRadius)));
	ball->Radius = radius;

	// 질량은 Radius에 비례 (예: Mass = Radius^3 * 밀도(1.0f))
	ball->Mass = radius * radius * radius * 1.0f;

	// -0.5f와 0.5f 사이의 임의의 실수를 생성합니다. (0~1 사이 난수에 -0.5)
	float randomX = -0.5f + (static_cast<float>(rand()) / RAND_MAX);
	float randomY = -0.5f + (static_cast<float>(rand()) / RAND_MAX);
	ball->Location = FVector(randomX, randomY, 0.0f);

	ball->Velocity = velocity;

	// 인자로 받은 렌더링 데이터를 멤버 변수에 저장합니다.
	ball->VertexBuffer = vertexBuffer;
	ball->NumVertices = numVertices;

	return ball;
}

void RemoveBall(int removeIndex)
{
	delete PrimitiveList[removeIndex];

	// 마지막 요소를 삭제된 자리에 덮어쓰기
	PrimitiveList[removeIndex] = PrimitiveList[UBall::TotalNumBalls - 1];
	PrimitiveList[UBall::TotalNumBalls - 1] = nullptr;

	UBall::TotalNumBalls--;
}

void ResizeBallList(int newCount, ID3D11Buffer* vertexBuffer, UINT numVertices)
{
	if (newCount < UBall::TotalNumBalls)
	{
		int removeCount = UBall::TotalNumBalls - newCount;
		for (int i = 0; i < removeCount; i++)
		{
			// 랜덤 인덱스 선택
			int removeIndex = rand() % UBall::TotalNumBalls;

			RemoveBall(removeIndex);
		}
	}
	else if (newCount > UBall::TotalNumBalls)
	{
		// 새로운 배열 할당
		UPrimitive** newList = new UPrimitive * [newCount];

		// 기존 복사
		for (int i = 0; i < UBall::TotalNumBalls; i++)
		{
			newList[i] = PrimitiveList[i];
		}

		// 새로 생성
		for (int i = UBall::TotalNumBalls; i < newCount; i++)
		{
			newList[i] = CreateRandomBall(vertexBuffer, numVertices);
		}

		// 이전 배열 해제
		delete[] PrimitiveList;

		PrimitiveList = newList;
		UBall::TotalNumBalls = newCount;
	}
	// 같으면 아무 것도 안 함
}

// IsInsideTrashCan 함수의 인자에 clientWidth와 clientHeight를 추가합니다.
bool IsInsideTrashCan(ImVec2 windowPos, ImVec2 windowSize, UPrimitive* primitive, float clientWidth, float clientHeight)
{
	UBall* ball = dynamic_cast<UBall*>(primitive);
	if (!ball)
	{
		return false;
	}

	// const float clientWidth = 1024.0f;  // 이 줄 삭제
	// const float clientHeight = 1024.0f; // 이 줄 삭제

	// 1. 공의 중심과 반지름을 화면 좌표(픽셀)로 변환
	float screenX = (ball->Location.x + 1.0f) * 0.5f * clientWidth;
	float screenY = (1.0f - ball->Location.y) * 0.5f * clientHeight;
	extern float scaleMod;
	float worldRadius = ball->Radius * scaleMod;
	float radiusInPixels = worldRadius * (clientWidth / 2.0f);

	// ... 나머지 코드는 동일 ...
	float trashLeft = windowPos.x;
	float trashRight = windowPos.x + windowSize.x;
	float trashTop = windowPos.y;
	float trashBottom = windowPos.y + windowSize.y;

	float closestX = screenX;
	if (closestX < trashLeft)
	{
		closestX = trashLeft;
	}
	if (closestX > trashRight)
	{
		closestX = trashRight;
	}

	float closestY = screenY;
	if (closestY < trashTop)
	{
		closestY = trashTop;
	}
	if (closestY > trashBottom)
	{
		closestY = trashBottom;
	}

	float distanceX = screenX - closestX;
	float distanceY = screenY - closestY;
	float distanceSquared = (distanceX * distanceX) + (distanceY * distanceY);

	if (distanceSquared <= (radiusInPixels * radiusInPixels))
	{
		return true;
	}

	return false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	// 윈도우 클래스 이름
	WCHAR WindowClass[] = L"JungleWindowClass";

	// 윈도우 타이틀바에 표시될 이름
	WCHAR Title[] = L"Game Tech Lab";

	// 각종 메시지를 처리할 함수인 WndProc의 함수 포인터를 WindowClass 구조체에 넣는다.
	WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };

	// 윈도우 클래스 등록
	RegisterClassW(&wndclass);

	// 1024 x 1024 크기에 윈도우 생성
	HWND hWnd = CreateWindowExW(0, WindowClass, Title, WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 1024, 1024,
		nullptr, nullptr, hInstance, nullptr);

	// Renderer Class를 생성합니다.
	URenderer renderer;

	// D3D11 생성하는 함수를 호출합니다.
	renderer.Create(hWnd);
	// 렌더러 생성 직후에 쉐이더를 생성하는 함수를 호출합니다.
	renderer.CreateShader();
	// 여기에 생성 함수를 추가합니다.	
	renderer.CreateConstantBuffer();

	// 여기에서 ImGui를 생성합니다.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Init((void*)hWnd);
	ImGui_ImplDX11_Init(renderer.Device, renderer.DeviceContext);

	// GetClientRect로 현재 윈도우 크기를 동적으로 가져옵니다.
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
	float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);

	UINT numVerticesSphere = sizeof(sphere_vertices) / sizeof(FVertexSimple);

	for (UINT i = 0; i < numVerticesSphere; i++)
	{
		sphere_vertices[i].x *= scaleMod;
		sphere_vertices[i].y *= scaleMod;
		sphere_vertices[i].z *= scaleMod;
	}

	ID3D11Buffer* vertexBufferSphere = renderer.CreateVertexBuffer(sphere_vertices, sizeof(sphere_vertices));

	bool bIsExit = false;

	// FPS 제한을 위한 설정
	const int targetFPS = 120;
	const double targetFrameTime = 1000.0 / targetFPS; // 한 프레임의 목표 시간 (밀리초 단위)

	// 고성능 타이머 초기화
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);

	LARGE_INTEGER startTime, endTime;
	double elapsedTime = 0.0;

	char ballCountBuffer[16] = "0"; // 초기값을 자연수로 설정
	int ballCount = 0;

	// Main 함수에 있는 Main 루프에 추가 하세요.
	while (bIsExit == false)
	{
		// 루프 시작 시간 기록
		QueryPerformanceCounter(&startTime);

		MSG msg;

		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				bIsExit = true;
				break;
			}
		}

		////////////////////////////////////////////
		// 매번 실행되는 코드를 여기에 추가합니다.

		// 준비 작업
		renderer.Prepare();
		renderer.PrepareShader();

		// 이동
		for (int i = 0; i < UBall::TotalNumBalls; i++)
		{
			PrimitiveList[i]->Move(elapsedTime);
			PrimitiveList[i]->Draw(renderer);
		}

		// 충돌 검사
		for (int i = 0; i < UBall::TotalNumBalls; i++)
		{
			for (int j = i + 1; j < UBall::TotalNumBalls; j++)
			{
				// 두 객체 충돌 발생
				if (PrimitiveList[i]->CheckCollision(PrimitiveList[j]))
				{
					PrimitiveList[j]->ResolveCollision(PrimitiveList[i]);
				}
			}
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// 이후 ImGui UI 컨트롤 추가는 ImGui::NewFrame()과 ImGui::Render() 사이인 여기에 위치합니다.
		ImGui::Begin("Jungle Property Window");

		ImGui::Text("Hello Jungle World!");

		// Hello Jungle World 아래에 CheckBox와 bBoundBallToScreen 변수를 연결합니다.
		ImGui::Checkbox("Gravity", &bUseGravity);

		ImGui::SetNextItemWidth(100);
		if (ImGui::InputText("##hidden", ballCountBuffer, IM_ARRAYSIZE(ballCountBuffer), ImGuiInputTextFlags_CallbackCharFilter, FilterDigits))
		{
			if (strlen(ballCountBuffer) == 0)
			{
				ballCountBuffer[0] = '0';
				ballCountBuffer[1] = '\0';
				ballCount = 0;
			}
			else
			{
				ballCount = std::atoi(ballCountBuffer);
			}

			ResizeBallList(ballCount, vertexBufferSphere, numVerticesSphere);
		}

		ImGui::SameLine();
		if (ImGui::Button("+"))
		{
			ballCount++;
			snprintf(ballCountBuffer, sizeof(ballCountBuffer), "%d", ballCount);

			ResizeBallList(ballCount, vertexBufferSphere, numVerticesSphere);
		}

		ImGui::SameLine();
		if (ImGui::Button("-"))
		{
			if (0 < ballCount)
			{
				ballCount--;
				snprintf(ballCountBuffer, sizeof(ballCountBuffer), "%d", ballCount);

				ResizeBallList(ballCount, vertexBufferSphere, numVerticesSphere);
			}
		}

		ImGui::SameLine();
		ImGui::Text("Number of Ball");

		ImGui::NewLine();

		ImGui::Checkbox("Use Trash Can", &bUseTrashCan);

		ImGui::End();

		if (bUseTrashCan)
		{
			ImGui::Begin("Trash Can");

			ImVec2 pos = ImGui::GetWindowPos();
			ImVec2 size = ImGui::GetWindowSize();

			// 디버깅용 출력
			ImGui::Text("Position: (%.1f, %.1f)", pos.x, pos.y);
			ImGui::Text("Size: (%.1f, %.1f)", size.x, size.y);

			for (int i = ballCount - 1; i >= 0; i--)
			{
				if (IsInsideTrashCan(pos, size, PrimitiveList[i], clientWidth, clientHeight))
				{
					RemoveBall(i);
				}
			}

			ballCount = UBall::TotalNumBalls;
			snprintf(ballCountBuffer, sizeof(ballCountBuffer), "%d", ballCount);

			ImGui::End();
		}

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// 다 그렸으면 버퍼를 교환
		renderer.SwapBuffer();

		////////////////////////////////////////////
		do
		{
			Sleep(0);

			// 루프 종료 시간 기록
			QueryPerformanceCounter(&endTime);

			// 한 프레임이 소요된 시간 계산 (밀리초 단위로 변환)
			elapsedTime = (endTime.QuadPart - startTime.QuadPart) * 1000.0 / frequency.QuadPart;

		} while (elapsedTime < targetFrameTime);
	}

	// 여기에서 ImGui 소멸
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	renderer.ReleaseVertexBuffer(vertexBufferSphere);

	// ReleaseShader() 직전에 소멸 함수를 추가합니다.
	renderer.ReleaseConstantBuffer();
	// 렌더러 소멸 직전에 쉐이더를 소멸 시키는 함수를 호출합니다.
	renderer.ReleaseShader();
	// D3D11 소멸 시키는 함수를 호출합니다.
	renderer.Release();

	for (int i = 0; i < UBall::TotalNumBalls; i++)
	{
		delete PrimitiveList[i];
	}
	delete[] PrimitiveList;
	PrimitiveList = nullptr;

	return 0;
}