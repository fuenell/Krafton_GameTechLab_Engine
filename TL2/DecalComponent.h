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

    FString TexturePath = "Editor/Icon/SpotLight_64x.dds";
};

