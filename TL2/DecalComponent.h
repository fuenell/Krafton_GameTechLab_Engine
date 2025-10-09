#pragma once
#include "PrimitiveComponent.h"

class UDecalComponent : public UPrimitiveComponent
{
public:
    DECLARE_CLASS(UDecalComponent, UPrimitiveComponent)
    UDecalComponent();

protected:
    ~UDecalComponent() override;

public:
    void Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj) override;

    void Serialize(bool bIsLoading, FPrimitiveData& InOut) override;

    UObject* Duplicate() override;
    void DuplicateSubObjects() override;

    void RenderOBB(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj);

    FMatrix GetViewProjectionMatrix();

    FString GetTexturePath() const { return TexturePath; };
    void SetTexturePath(FString InTexturePath) { TexturePath = InTexturePath; };

    // FadeAlpha Getter/Setter
    float GetFadeAlpha() const { return FadeAlpha; };
    void SetFadeAlpha(float InAlpha)
    {
        // 값의 범위를 0.0f ~ 1.0f 사이로 제한 (Clamp)
        FadeAlpha = std::max(0.0f, std::min(InAlpha, 1.0f));
    };

protected:
    FString TexturePath = "Editor/Icon/SpotLight_64x.dds";
    // 1.0f = 완전 불투명, 0.0f = 완전 투명
    float FadeAlpha = 1.0f;
};

