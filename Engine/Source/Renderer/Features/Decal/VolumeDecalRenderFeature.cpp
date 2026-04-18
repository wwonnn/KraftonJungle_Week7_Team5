#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"

#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Mesh/Vertex.h"
#include <algorithm>
#include <chrono>

namespace
{
    using FVolumeDecalClock = std::chrono::high_resolution_clock;

    static double ToMilliseconds(FVolumeDecalClock::duration Duration)
    {
        return std::chrono::duration<double, std::milli>(Duration).count();
    }

    static constexpr UINT DECAL_DEPTH_TEXTURE_SLOT = 0;
    static constexpr UINT DECAL_TEXTURE_ARRAY_SLOT = 1;
    static constexpr UINT DECAL_PER_MATERIAL_CB_SLOT = 2;
    static constexpr UINT DECAL_DEPTH_SAMPLER_SLOT = 0;
    static constexpr UINT DECAL_TEXTURE_SAMPLER_SLOT = 1;
}

FVolumeDecalRenderFeature::~FVolumeDecalRenderFeature()
{
    Release();
}

bool FVolumeDecalRenderFeature::Initialize(FRenderer& Renderer)
{
    if (bInitialized)
    {
        return true;
    }

    Release();

    if (!CreateShaders(Renderer)
        || !CreateVolumeMesh(Renderer)
        || !CreatePerDecalConstantBuffer(Renderer)
        || !CreateStates(Renderer)
        || !CreateSamplers(Renderer))
    {
        Release();
        return false;
    }

    bInitialized = true;
    return true;
}

void FVolumeDecalRenderFeature::Release()
{
    bInitialized = false;
    VolumeVS.reset();
    VolumePS.reset();

    if (VolumeVertexBuffer) { VolumeVertexBuffer->Release(); VolumeVertexBuffer = nullptr; }
    if (VolumeIndexBuffer) { VolumeIndexBuffer->Release(); VolumeIndexBuffer = nullptr; }
    VolumeIndexCount = 0;

    if (PerDecalConstantBuffer) { PerDecalConstantBuffer->Release(); PerDecalConstantBuffer = nullptr; }

    if (DepthPointSampler) { DepthPointSampler->Release(); DepthPointSampler = nullptr; }
    if (DecalLinearSampler) { DecalLinearSampler->Release(); DecalLinearSampler = nullptr; }

    if (VolumeBlendState) { VolumeBlendState->Release(); VolumeBlendState = nullptr; }
    if (VolumeDepthState) { VolumeDepthState->Release(); VolumeDepthState = nullptr; }
    if (VolumeRasterizerState) { VolumeRasterizerState->Release(); VolumeRasterizerState = nullptr; }

    DebugPS.reset();
    if (DebugDepthState) { DebugDepthState->Release(); DebugDepthState = nullptr; }
}

bool FVolumeDecalRenderFeature::Render(
    FRenderer& Renderer,
    const FDecalRenderRequest& Request,
    const FSceneRenderTargets& Targets)
{
    const FVolumeDecalClock::time_point StartTime = FVolumeDecalClock::now();
    LastStats = {};
    LastBuildTimeMs = 0.0;
    LastCullIntersectionTimeMs = 0.0;
    LastShadingPassTimeMs = 0.0;
    LastTotalTimeMs = 0.0;

    if (Request.IsEmpty())
    {
        return true;
    }

    if (!Targets.SceneColorRTV || !Targets.SceneDepthSRV)
    {
        return false;
    }

    if (!Initialize(Renderer))
    {
        return false;
    }

    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!DeviceContext)
    {
        return false;
    }

    Renderer.SetConstantBuffers();

    const D3D11_VIEWPORT Viewport =
    {
        0.0f,
        0.0f,
        static_cast<float>(Request.ViewportWidth),
        static_cast<float>(Request.ViewportHeight),
        0.0f,
        1.0f
    };

    DeviceContext->RSSetViewports(1, &Viewport);
    DeviceContext->OMSetRenderTargets(1, &Targets.SceneColorRTV, nullptr);
    DeviceContext->OMSetBlendState(VolumeBlendState, nullptr, 0xFFFFFFFFu);
    DeviceContext->OMSetDepthStencilState(VolumeDepthState, 0);
    DeviceContext->RSSetState(VolumeRasterizerState);

    if (VolumeVS)
    {
        VolumeVS->Bind(DeviceContext);
    }
    if (VolumePS)
    {
        VolumePS->Bind(DeviceContext);
    }

    UINT Stride = sizeof(FVertex);
    UINT Offset = 0;
    DeviceContext->IASetVertexBuffers(0, 1, &VolumeVertexBuffer, &Stride, &Offset);
    DeviceContext->IASetIndexBuffer(VolumeIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11ShaderResourceView* ShaderResources[2] =
    {
        Targets.SceneDepthSRV,
        Request.BaseColorTextureArraySRV
    };
    DeviceContext->PSSetShaderResources(DECAL_DEPTH_TEXTURE_SLOT, 2, ShaderResources);

    ID3D11SamplerState* Samplers[2] =
    {
        DepthPointSampler,
        DecalLinearSampler
    };
    DeviceContext->PSSetSamplers(DECAL_DEPTH_SAMPLER_SLOT, 2, Samplers);

    const FVolumeDecalClock::time_point BuildStartTime = FVolumeDecalClock::now();
    TArray<const FDecalRenderItem*> SortedItems;
    SortedItems.reserve(Request.Items.size());
    for (const FDecalRenderItem& Item : Request.Items)
    {
        SortedItems.push_back(&Item);
    }

    std::sort(
        SortedItems.begin(),
        SortedItems.end(),
        [](const FDecalRenderItem* A, const FDecalRenderItem* B)
        {
            if (A->Priority != B->Priority)
            {
                return A->Priority > B->Priority;
            }
            return A->TextureIndex < B->TextureIndex;
        });
    LastBuildTimeMs = ToMilliseconds(FVolumeDecalClock::now() - BuildStartTime);

    LastStats.CandidateObjects = Request.CandidateReceiverObjectCount;
    const FVolumeDecalClock::time_point CullStartTime = FVolumeDecalClock::now();
    TArray<const FDecalRenderItem*> RenderableItems;
    RenderableItems.reserve(SortedItems.size());
    for (const FDecalRenderItem* Item : SortedItems)
    {
        if (!Item || !Item->IsValid())
        {
            continue;
        }

        if ((Item->Flags & DECAL_RENDER_FLAG_BaseColor) == 0u)
        {
            continue;
        }

        ++LastStats.IntersectPassed;
        RenderableItems.push_back(Item);
    }
    LastCullIntersectionTimeMs = ToMilliseconds(FVolumeDecalClock::now() - CullStartTime);

    const FVolumeDecalClock::time_point ShadingStartTime = FVolumeDecalClock::now();
    for (const FDecalRenderItem* Item : RenderableItems)
    {
        if (!Item)
        {
            continue;
        }

        Renderer.UpdateObjectConstantBuffer(Item->DecalWorld);
        if (!UpdatePerDecalConstants(Renderer, Request, *Item))
        {
            continue;
        }

        ID3D11Buffer* PerDecalCBs[1] = { PerDecalConstantBuffer };
        DeviceContext->VSSetConstantBuffers(DECAL_PER_MATERIAL_CB_SLOT, 1, PerDecalCBs);
        DeviceContext->PSSetConstantBuffers(DECAL_PER_MATERIAL_CB_SLOT, 1, PerDecalCBs);

        DeviceContext->DrawIndexed(VolumeIndexCount, 0, 0);
        ++LastStats.DecalDrawCalls;
    }
    LastShadingPassTimeMs = ToMilliseconds(FVolumeDecalClock::now() - ShadingStartTime);

    ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
    DeviceContext->PSSetShaderResources(DECAL_DEPTH_TEXTURE_SLOT, 2, NullSRVs);

    LastTotalTimeMs = ToMilliseconds(FVolumeDecalClock::now() - StartTime);
    return true;
}

bool FVolumeDecalRenderFeature::RenderDebugOverlay(
    FRenderer& Renderer,
    const FDecalRenderRequest& Request,
    const FSceneRenderTargets& Targets,
    ID3D11RenderTargetView* RenderTargetView)
{
    if (!Request.bDebugDraw || Request.Items.empty())
    {
        return true;
    }

    if (!RenderTargetView || !Targets.SceneDepthDSV)
    {
        return true;
    }

    if (!Initialize(Renderer) || !DebugPS || !DebugDepthState || !VolumeVS || !VolumeVertexBuffer || !VolumeIndexBuffer)
    {
        return false;
    }

    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!DeviceContext)
    {
        return false;
    }

    const D3D11_VIEWPORT Viewport =
    {
        0.0f,
        0.0f,
        static_cast<float>(Request.ViewportWidth),
        static_cast<float>(Request.ViewportHeight),
        0.0f,
        1.0f
    };

    Renderer.SetConstantBuffers();
    DeviceContext->RSSetViewports(1, &Viewport);
    DeviceContext->OMSetRenderTargets(1, &RenderTargetView, Targets.SceneDepthDSV);
    DeviceContext->OMSetDepthStencilState(DebugDepthState, 0);
    DeviceContext->OMSetBlendState(VolumeBlendState, nullptr, 0xFFFFFFFFu);
    DeviceContext->RSSetState(VolumeRasterizerState);
    VolumeVS->Bind(DeviceContext);
    DebugPS->Bind(DeviceContext);

    UINT Stride = sizeof(FVertex);
    UINT Offset = 0;
    DeviceContext->IASetVertexBuffers(0, 1, &VolumeVertexBuffer, &Stride, &Offset);
    DeviceContext->IASetIndexBuffer(VolumeIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    TArray<const FDecalRenderItem*> SortedItems;
    SortedItems.reserve(Request.Items.size());
    for (const FDecalRenderItem& Item : Request.Items)
    {
        SortedItems.push_back(&Item);
    }

    std::sort(
        SortedItems.begin(),
        SortedItems.end(),
        [](const FDecalRenderItem* A, const FDecalRenderItem* B)
        {
            if (A->Priority != B->Priority)
            {
                return A->Priority > B->Priority;
            }
            return A->TextureIndex < B->TextureIndex;
        });

    for (const FDecalRenderItem* Item : SortedItems)
    {
        if (!Item || !Item->IsValid())
        {
            continue;
        }

        Renderer.UpdateObjectConstantBuffer(Item->DecalWorld);

        FDecalRenderItem DebugItem = *Item;
        DebugItem.BaseColorTint = FLinearColor::FromSRGB(FVector4(1.0f, 0.6f, 0.1f, 1.0f));
        if (!UpdatePerDecalConstants(Renderer, Request, DebugItem))
        {
            continue;
        }

        ID3D11Buffer* ConstantBuffers[1] = { PerDecalConstantBuffer };
        DeviceContext->VSSetConstantBuffers(DECAL_PER_MATERIAL_CB_SLOT, 1, ConstantBuffers);
        DeviceContext->PSSetConstantBuffers(DECAL_PER_MATERIAL_CB_SLOT, 1, ConstantBuffers);
        DeviceContext->DrawIndexed(VolumeIndexCount, 0, 0);
    }

    return true;
}

bool FVolumeDecalRenderFeature::CreateVolumeMesh(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    const FVector4 White(1, 1, 1, 1);
    const FVector NormalX(1, 0, 0);

    const FVertex Vertices[] =
    {
        { FVector(-1, -1, -1), White, NormalX, FVector2(0, 0) },
        { FVector( 1, -1, -1), White, NormalX, FVector2(1, 0) },
        { FVector( 1,  1, -1), White, NormalX, FVector2(1, 1) },
        { FVector(-1,  1, -1), White, NormalX, FVector2(0, 1) },
        { FVector(-1, -1,  1), White, NormalX, FVector2(0, 0) },
        { FVector( 1, -1,  1), White, NormalX, FVector2(1, 0) },
        { FVector( 1,  1,  1), White, NormalX, FVector2(1, 1) },
        { FVector(-1,  1,  1), White, NormalX, FVector2(0, 1) },
    };

    const uint32 Indices[] =
    {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        1, 2, 6, 1, 6, 5,
        2, 3, 7, 2, 7, 6,
        3, 0, 4, 3, 4, 7,
    };

    D3D11_BUFFER_DESC VBDesc = {};
    VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    VBDesc.ByteWidth = sizeof(Vertices);
    VBDesc.Usage = D3D11_USAGE_IMMUTABLE;

    D3D11_SUBRESOURCE_DATA VBData = {};
    VBData.pSysMem = Vertices;

    if (FAILED(Device->CreateBuffer(&VBDesc, &VBData, &VolumeVertexBuffer)) || !VolumeVertexBuffer)
    {
        return false;
    }

    D3D11_BUFFER_DESC IBDesc = {};
    IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    IBDesc.ByteWidth = sizeof(Indices);
    IBDesc.Usage = D3D11_USAGE_IMMUTABLE;

    D3D11_SUBRESOURCE_DATA IBData = {};
    IBData.pSysMem = Indices;

    if (FAILED(Device->CreateBuffer(&IBDesc, &IBData, &VolumeIndexBuffer)) || !VolumeIndexBuffer)
    {
        return false;
    }

    VolumeIndexCount = static_cast<UINT>(sizeof(Indices) / sizeof(Indices[0]));
    return true;
}

bool FVolumeDecalRenderFeature::CreatePerDecalConstantBuffer(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    D3D11_BUFFER_DESC Desc = {};
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.ByteWidth = sizeof(FVolumeDecalConstants);
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Desc.Usage = D3D11_USAGE_DYNAMIC;

    return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &PerDecalConstantBuffer)) && PerDecalConstantBuffer;
}

bool FVolumeDecalRenderFeature::CreateStates(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    D3D11_BLEND_DESC BlendDesc = {};
    BlendDesc.RenderTarget[0].BlendEnable = TRUE;
    BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(Device->CreateBlendState(&BlendDesc, &VolumeBlendState)) || !VolumeBlendState)
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
    DepthDesc.DepthEnable = FALSE;
    DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &VolumeDepthState)) || !VolumeDepthState)
    {
        return false;
    }

    D3D11_RASTERIZER_DESC RasterDesc = {};
    RasterDesc.FillMode = D3D11_FILL_SOLID;
    RasterDesc.CullMode = D3D11_CULL_NONE;
    RasterDesc.DepthClipEnable = TRUE;
    if (FAILED(Device->CreateRasterizerState(&RasterDesc, &VolumeRasterizerState)) || !VolumeRasterizerState)
    {
        return false;
    }

	D3D11_DEPTH_STENCIL_DESC DebugDepthDesc = {};
	DebugDepthDesc.DepthEnable = TRUE;
	DebugDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DebugDepthDesc.DepthFunc = D3D11_COMPARISON_LESS;
	if (FAILED(Device->CreateDepthStencilState(&DebugDepthDesc, &DebugDepthState)) || !DebugDepthState)
	{
		return false;
	}
	
	
    return true;
}

bool FVolumeDecalRenderFeature::CreateSamplers(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    D3D11_SAMPLER_DESC DepthDesc = {};
    DepthDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    DepthDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    DepthDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    DepthDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    DepthDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    DepthDesc.MinLOD = 0;
    DepthDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(Device->CreateSamplerState(&DepthDesc, &DepthPointSampler)) || !DepthPointSampler)
    {
        return false;
    }

    D3D11_SAMPLER_DESC LinearDesc = DepthDesc;
    LinearDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(Device->CreateSamplerState(&LinearDesc, &DecalLinearSampler)) || !DecalLinearSampler)
    {
        return false;
    }

    return true;
}

bool FVolumeDecalRenderFeature::CreateShaders(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    const std::wstring ShaderDir = FPaths::ShaderDir();
    const std::wstring VSPath = ShaderDir + L"SceneEffects/VolumeDecalVertexShader.hlsl";
    const std::wstring PSPath = ShaderDir + L"SceneEffects/VolumeDecalPixelShader.hlsl";
	const std::wstring DebugPSPath = ShaderDir + L"SceneEffects/DecalDebugPixelShader.hlsl";

    VolumeVS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str());
    VolumePS = FShaderMap::Get().GetOrCreatePixelShader(Device, PSPath.c_str());
	DebugPS =  FShaderMap::Get().GetOrCreatePixelShader(Device, DebugPSPath.c_str());
    return VolumeVS != nullptr && VolumePS != nullptr && DebugPS != nullptr;
}

bool FVolumeDecalRenderFeature::UpdatePerDecalConstants(
    FRenderer& Renderer,
    const FDecalRenderRequest& Request,
    const FDecalRenderItem& Item)
{
    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!DeviceContext || !PerDecalConstantBuffer)
    {
        return false;
    }

    FVolumeDecalConstants Constants;
    Constants.InverseViewProjection = Request.InverseViewProjection.GetTransposed();
    Constants.WorldToDecal = Item.WorldToDecal.GetTransposed();
    Constants.AtlasScaleBias = Item.AtlasScaleBias;
    Constants.BaseColorTint = Item.BaseColorTint;
    Constants.DecalExtentsAndEdgeFade = FVector4(Item.Extents.X, Item.Extents.Y, Item.Extents.Z, Item.EdgeFade);

    const float InvViewportWidth = Request.ViewportWidth > 0 ? 1.0f / static_cast<float>(Request.ViewportWidth) : 0.0f;
    const float InvViewportHeight = Request.ViewportHeight > 0 ? 1.0f / static_cast<float>(Request.ViewportHeight) : 0.0f;
    Constants.InvViewportSizeAndAllowAngleAndTextureIndex = FVector4(
        InvViewportWidth,
        InvViewportHeight,
        Item.AllowAngle,
        static_cast<float>(Item.TextureIndex));

    const FVector DecalAxisX = FVector(
        Item.DecalWorld.M[0][0],
        Item.DecalWorld.M[0][1],
        Item.DecalWorld.M[0][2]).GetSafeNormal();
    Constants.DecalForwardWSAndPad = FVector4(DecalAxisX.X, DecalAxisX.Y, DecalAxisX.Z, 0.0f);

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(DeviceContext->Map(PerDecalConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }

    memcpy(Mapped.pData, &Constants, sizeof(Constants));
    DeviceContext->Unmap(PerDecalConstantBuffer, 0);
    return true;
}
