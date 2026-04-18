#include "Renderer/Features/Outline/OutlineRenderFeature.h"

#include "Core/Paths.h"
#include "Math/LinearColor.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

namespace
{
	struct FOutlinePostConstantBuffer
	{
		FVector4 OutlineColor = FLinearColor::FromSRGB(FVector4(1.0f, 0.5f, 0.0f, 1.0f)).ToVector4();
		float OutlineThickness = 4.0f;
		float OutlineThreshold = 0.1f;
		float Padding[2] = {};
	};

	const FOutlinePostConstantBuffer GOutlinePostDefaults = {};
}

FOutlineRenderFeature::~FOutlineRenderFeature()
{
	Release();
}

bool FOutlineRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!OutlinePostConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = sizeof(FOutlinePostConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &OutlinePostConstantBuffer)))
		{
			return false;
		}
	}

	if (!StencilWriteState)
	{
		D3D11_DEPTH_STENCIL_DESC WriteDesc = {};
		WriteDesc.DepthEnable = FALSE;
		WriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		WriteDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		WriteDesc.StencilEnable = TRUE;
		WriteDesc.StencilReadMask = 0xFF;
		WriteDesc.StencilWriteMask = 0xFF;
		WriteDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
		WriteDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
		WriteDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		WriteDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		WriteDesc.BackFace = WriteDesc.FrontFace;
		if (FAILED(Device->CreateDepthStencilState(&WriteDesc, &StencilWriteState)))
		{
			return false;
		}
	}

	if (!StencilEqualState)
	{
		D3D11_DEPTH_STENCIL_DESC EqualDesc = {};
		EqualDesc.DepthEnable = FALSE;
		EqualDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		EqualDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		EqualDesc.StencilEnable = TRUE;
		EqualDesc.StencilReadMask = 0xFF;
		EqualDesc.StencilWriteMask = 0x00;
		EqualDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		EqualDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		EqualDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		EqualDesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
		EqualDesc.BackFace = EqualDesc.FrontFace;
		if (FAILED(Device->CreateDepthStencilState(&EqualDesc, &StencilEqualState)))
		{
			return false;
		}
	}

	if (!StencilNotEqualState)
	{
		D3D11_DEPTH_STENCIL_DESC NotEqualDesc = {};
		NotEqualDesc.DepthEnable = FALSE;
		NotEqualDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		NotEqualDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		NotEqualDesc.StencilEnable = TRUE;
		NotEqualDesc.StencilReadMask = 0xFF;
		NotEqualDesc.StencilWriteMask = 0x00;
		NotEqualDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		NotEqualDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		NotEqualDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		NotEqualDesc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
		NotEqualDesc.BackFace = NotEqualDesc.FrontFace;
		if (FAILED(Device->CreateDepthStencilState(&NotEqualDesc, &StencilNotEqualState)))
		{
			return false;
		}
	}

	if (!OutlineBlendState)
	{
		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(Device->CreateBlendState(&BlendDesc, &OutlineBlendState)))
		{
			return false;
		}
	}

	if (!OutlineRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &OutlineRasterizerState)))
		{
			return false;
		}
	}

	if (!OutlineSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &OutlineSampler)))
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir();
	if (!OutlinePostVS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl").c_str(), "main", "vs_5_0");
		if (!Resource || FAILED(Device->CreateVertexShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &OutlinePostVS)))
		{
			return false;
		}
	}

	if (!OutlineMaskPS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"SelectionHighlight/OutlineMaskPixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &OutlineMaskPS)))
		{
			return false;
		}
	}

	if (!OutlineSobelPS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"SelectionHighlight/OutlineSobelPixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &OutlineSobelPS)))
		{
			return false;
		}
	}

	return true;
}

void FOutlineRenderFeature::UpdateOutlinePostConstantBuffer(
	FRenderer& Renderer,
	const FVector4& OutlineColor,
	float OutlineThickness,
	float OutlineThreshold)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!OutlinePostConstantBuffer || !DeviceContext)
	{
		return;
	}

	FOutlinePostConstantBuffer CBData = {};
	CBData.OutlineColor = OutlineColor;
	CBData.OutlineThickness = OutlineThickness;
	CBData.OutlineThreshold = OutlineThreshold;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(OutlinePostConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(OutlinePostConstantBuffer, 0);
	}
}

bool FOutlineRenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneRenderTargets& Targets,
	const FOutlineRenderRequest& Request)
{
	if (!RenderMaskPass(Renderer, Frame, View, Targets, Request))
	{
		return false;
	}

	return RenderCompositePass(Renderer, Frame, View, Targets, Request);
}

bool FOutlineRenderFeature::RenderMaskPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneRenderTargets& Targets,
	const FOutlineRenderRequest& Request)
{
	if (!Request.bEnabled || Request.Items.empty() || !Targets.OutlineMaskRTV || !Targets.OutlineMaskSRV || !Initialize(Renderer))
	{
		return true;
	}

	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!Device || !DeviceContext || !Targets.SceneColorRTV || !Targets.SceneDepthDSV)
	{
		return false;
	}

	constexpr float ClearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	constexpr float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };

	DeviceContext->ClearDepthStencilView(Targets.SceneDepthDSV, D3D11_CLEAR_STENCIL, 1.0f, 0);
	BeginPass(Renderer, 0, nullptr, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xFFFFFFFF);
	DeviceContext->OMSetDepthStencilState(StencilWriteState, 1);

	for (const FOutlineRenderItem& Item : Request.Items)
	{
		if (!Item.Mesh || !Item.Mesh->UpdateVertexAndIndexBuffer(Device, DeviceContext))
		{
			continue;
		}

		FMaterial* Material = Item.Material ? Item.Material : Renderer.GetDefaultMaterial();
		if (!Material)
		{
			continue;
		}

		Material->Bind(DeviceContext, EMaterialPassType::OutlineMask);
		if (!Material->HasPixelTextureBinding())
		{
			ID3D11SamplerState* DefaultSampler = Renderer.GetDefaultSampler();
			DeviceContext->PSSetSamplers(0, 1, &DefaultSampler);
		}

		if (Item.bDisableCulling)
		{
			FRasterizerStateOption RasterOpt = Material->GetRasterizerOption();
			RasterOpt.CullMode = D3D11_CULL_NONE;
			Renderer.GetRenderStateManager()->BindState(
				Renderer.GetRenderStateManager()->GetOrCreateRasterizerState(RasterOpt));
		}
		else
		{
			Renderer.GetRenderStateManager()->BindState(Material->GetRasterizerState());
		}

		Item.Mesh->Bind(DeviceContext);
		if (Item.Mesh->Topology != EMeshTopology::EMT_Undefined)
		{
			DeviceContext->IASetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY)Item.Mesh->Topology);
		}

		Renderer.UpdateObjectConstantBuffer(Item.WorldMatrix);
		if (!Item.Mesh->Indices.empty())
		{
			const UINT DrawCount = (Item.IndexCount > 0u) ? Item.IndexCount : static_cast<UINT>(Item.Mesh->Indices.size());
			DeviceContext->DrawIndexed(DrawCount, Item.IndexStart, 0);
		}
		else
		{
			DeviceContext->Draw(static_cast<UINT>(Item.Mesh->Vertices.size()), 0);
		}
	}

	EndPass(Renderer, Targets.SceneColorRTV, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	DeviceContext->ClearRenderTargetView(Targets.OutlineMaskRTV, ClearColor);
	FFullscreenPassPipelineState MaskPipelineState;
	MaskPipelineState.DepthStencilState = StencilEqualState;
	MaskPipelineState.StencilRef = 1;
	MaskPipelineState.RasterizerState = OutlineRasterizerState;
	return ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.OutlineMaskRTV,
		Targets.SceneDepthDSV,
		View.Viewport,
		{ OutlinePostVS, OutlineMaskPS },
		MaskPipelineState,
		{},
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		});
}

bool FOutlineRenderFeature::RenderCompositePass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneRenderTargets& Targets,
	const FOutlineRenderRequest& Request)
{
	if (!Request.bEnabled || Request.Items.empty() || !Targets.OutlineMaskSRV || !Initialize(Renderer))
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext || !Targets.SceneColorRTV || !Targets.SceneDepthDSV)
	{
		return false;
	}

	constexpr float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };

	FFullscreenPassPipelineState CompositePipelineState;
	CompositePipelineState.BlendState = OutlineBlendState;
	CompositePipelineState.BlendFactor = BlendFactor;
	CompositePipelineState.DepthStencilState = StencilNotEqualState;
	CompositePipelineState.StencilRef = 1;
	UpdateOutlinePostConstantBuffer(
		Renderer,
		GOutlinePostDefaults.OutlineColor,
		GOutlinePostDefaults.OutlineThickness,
		GOutlinePostDefaults.OutlineThreshold);

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, OutlinePostConstantBuffer },
	};
	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, Targets.OutlineMaskSRV },
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, OutlineSampler },
	};
	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	const bool bSucceeded = ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		View.Viewport,
		{ OutlinePostVS, OutlineSobelPS },
		CompositePipelineState,
		Bindings,
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		});

	DeviceContext->ClearDepthStencilView(Targets.SceneDepthDSV, D3D11_CLEAR_STENCIL, 1.0f, 0);
	return bSucceeded;
}

void FOutlineRenderFeature::Release()
{
	if (OutlinePostConstantBuffer)
	{
		OutlinePostConstantBuffer->Release();
		OutlinePostConstantBuffer = nullptr;
	}
	if (StencilWriteState)
	{
		StencilWriteState->Release();
		StencilWriteState = nullptr;
	}
	if (StencilEqualState)
	{
		StencilEqualState->Release();
		StencilEqualState = nullptr;
	}
	if (StencilNotEqualState)
	{
		StencilNotEqualState->Release();
		StencilNotEqualState = nullptr;
	}
	if (OutlineBlendState)
	{
		OutlineBlendState->Release();
		OutlineBlendState = nullptr;
	}
	if (OutlineRasterizerState)
	{
		OutlineRasterizerState->Release();
		OutlineRasterizerState = nullptr;
	}
	if (OutlineSampler)
	{
		OutlineSampler->Release();
		OutlineSampler = nullptr;
	}
	if (OutlinePostVS)
	{
		OutlinePostVS->Release();
		OutlinePostVS = nullptr;
	}
	if (OutlineMaskPS)
	{
		OutlineMaskPS->Release();
		OutlineMaskPS = nullptr;
	}
	if (OutlineSobelPS)
	{
		OutlineSobelPS->Release();
		OutlineSobelPS = nullptr;
	}
}
