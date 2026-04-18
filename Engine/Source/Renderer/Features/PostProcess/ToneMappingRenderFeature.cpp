#include "ToneMappingRenderFeature.h"

#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"

namespace
{
	struct FToneMappingConstantBufferData
	{
		float Exposure         = 1.0f;
		float ShoulderStrength = 1.0f;
		float LinearWhite      = 1.0f;
		float Pad0             = 0.0f;
	};
}

FToneMappingRenderFeature::~FToneMappingRenderFeature()
{
	Release();
}

bool FToneMappingRenderFeature::Render(FRenderer& Renderer, const FFrameContext& Frame, const FViewContext& View, FSceneRenderTargets& Targets)
{
	if (!Targets.GetSceneColorShaderResource() || !Targets.GetSceneColorWriteRenderTarget())
	{
		return true;
	}

	if (!Initialize(Renderer))
	{
		return false;
	}

	UpdateConstantBuffer(Renderer);

	FFullscreenPassPipelineState PipelineState;
	PipelineState.BlendState        = nullptr;
	PipelineState.DepthStencilState = NoDepthState;
	PipelineState.RasterizerState   = ToneMappingRasterizerState;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{0, ToneMappingConstantBuffer},
	};

	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{0, Targets.GetSceneColorShaderResource()},
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{0, LinearSampler},
	};
	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	const bool bOk = ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.GetSceneColorWriteRenderTarget(),
		nullptr,
		View.Viewport,
		{FullscreenVS, ToneMappingPS},
		PipelineState,
		Bindings,
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		}
	);

	if (!bOk)
	{
		return false;
	}

	Targets.SwapSceneColor();
	return true;
}

void FToneMappingRenderFeature::Release()
{
	if (ToneMappingConstantBuffer)
	{
		ToneMappingConstantBuffer->Release();
		ToneMappingConstantBuffer = nullptr;
	}
	if (NoDepthState)
	{
		NoDepthState->Release();
		NoDepthState = nullptr;
	}
	if (ToneMappingRasterizerState)
	{
		ToneMappingRasterizerState->Release();
		ToneMappingRasterizerState = nullptr;
	}
	if (LinearSampler)
	{
		LinearSampler->Release();
		LinearSampler = nullptr;
	}
	if (FullscreenVS)
	{
		FullscreenVS->Release();
		FullscreenVS = nullptr;
	}
	if (ToneMappingPS)
	{
		ToneMappingPS->Release();
		ToneMappingPS = nullptr;
	}
}

bool FToneMappingRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!ToneMappingConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage             = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth         = sizeof(FToneMappingConstantBufferData);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &ToneMappingConstantBuffer)))
		{
			return false;
		}
	}

	if (!NoDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable              = FALSE;
		DepthDesc.DepthWriteMask           = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthDesc.DepthFunc                = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
		{
			return false;
		}
	}

	if (!ToneMappingRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode              = D3D11_FILL_SOLID;
		RasterDesc.CullMode              = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable       = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &ToneMappingRasterizerState)))
		{
			return false;
		}
	}

	if (!LinearSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD             = 0.0f;
		SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &LinearSampler)))
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	if (!FullscreenVS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl").c_str(), "main", "vs_5_0");

		if (!Resource || FAILED(Device->CreateVertexShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &FullscreenVS)))
		{
			return false;
		}
	}

	if (!ToneMappingPS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"FinalImagePostProcess/ToneMappingPixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &ToneMappingPS)))
		{
			return false;
		}
	}
	return true;
}

void FToneMappingRenderFeature::UpdateConstantBuffer(FRenderer& Renderer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!ToneMappingConstantBuffer || !DeviceContext)
	{
		return;
	}

	FToneMappingConstantBufferData CBData = {};
	CBData.Exposure                       = 1.0f;
	CBData.ShoulderStrength               = 1.0f;
	CBData.LinearWhite                    = 11.2f;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(ToneMappingConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(ToneMappingConstantBuffer, 0);
	}
}
