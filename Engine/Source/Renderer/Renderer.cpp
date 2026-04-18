#include "Renderer/Renderer.h"
#include "Actor/Actor.h"
#include "Component/DecalComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Core/Paths.h"
#include "Debug/DebugDrawManager.h"
#include "Renderer/Features/Billboard/BillboardRenderFeature.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/PostProcess/ToneMappingRenderFeature.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Features/SubUV/SubUVRenderFeature.h"
#include "Renderer/Features/Text/TextRenderFeature.h"
#include "Renderer/Frame/EditorFrameRenderer.h"
#include "Renderer/Frame/GameFrameRenderer.h"
#include "Renderer/Frame/RendererResourceBootstrap.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Frame/Viewport/ViewportCompositor.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Renderer/Resources/Shader/ShaderType.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/PassContext.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"
#include "World/World.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <vector>


#define STB_IMAGE_IMPLEMENTATION
#include "Asset/ObjManager.h"
#include "Core/ConsoleVariableManager.h"
#include "Core/Engine.h"
#include "Debug/EngineLog.h"
#include "ThirdParty/stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
	ID3D11Texture2D* ResolveTextureFromRenderTarget(ID3D11RenderTargetView* RenderTargetView)
	{
		if (!RenderTargetView)
		{
			return nullptr;
		}

		ID3D11Resource* Resource = nullptr;
		RenderTargetView->GetResource(&Resource);
		if (!Resource)
		{
			return nullptr;
		}

		ID3D11Texture2D* Texture = nullptr;
		const HRESULT    Hr      = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
		Resource->Release();
		return SUCCEEDED(Hr) ? Texture : nullptr;
	}
}

FRenderer::FRenderer(HWND InHwnd, int32 InWidth, int32 InHeight)
	: SceneRenderer(std::make_unique<FSceneRenderer>()), ViewportCompositor(std::make_unique<FViewportCompositor>()),
	  ScreenUIRenderer(std::make_unique<FScreenUIRenderer>()),
	  SceneTargetManager(std::make_unique<FSceneTargetManager>()),
	  DecalTextureCache(std::make_unique<FDecalTextureCache>())
{
	Initialize(InHwnd, InWidth, InHeight);
}

FRenderer::~FRenderer()
{
	Release();
}

bool FRenderer::Initialize(HWND InHwnd, int32 Width, int32 Height)
{
	if (!RenderDevice.Initialize(InHwnd, Width, Height))
	{
		return false;
	}
	ID3D11Device*        Device        = RenderDevice.GetDevice();
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!Device || !DeviceContext)
	{
		return false;
	}

	RenderStateManager = std::make_unique<FRenderStateManager>(Device, DeviceContext);
	RenderStateManager->PrepareCommonStates();

	if (!CreateSamplers())
	{
		return false;
	}
	if (!ViewportCompositor || !ViewportCompositor->Initialize(Device))
	{
		return false;
	}

	if (!CreateSceneColorResolveResources())
	{
		return false;
	}

	if (!CreateConstantBuffers())
	{
		return false;
	}
	SetConstantBuffers();

	if (!FRendererResourceBootstrap::Initialize(*this))
	{
		return false;
	}

	return true;
}

void FRenderer::BeginFrame()
{
	constexpr float ClearColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};
	RenderDevice.BeginFrame(ClearColor);
	if (SceneRenderer)
	{
		SceneRenderer->BeginFrame();
	}
}

void FRenderer::EndFrame()
{
	RenderDevice.EndFrame();
}

void FRenderer::Release()
{
	ReleaseSceneColorResolveResources();

	if (SceneTargetManager)
	{
		SceneTargetManager->Release();
	}
	if (ViewportCompositor)
	{
		ViewportCompositor->Release();
	}
	FRendererResourceBootstrap::Release(*this);
	RenderDevice.Release();
}

bool FRenderer::IsOccluded()
{
	return RenderDevice.IsOccluded();
}

void FRenderer::OnResize(int32 W, int32 H)
{
	if (W == 0 || H == 0)
	{
		return;
	}
	if (SceneTargetManager)
	{
		SceneTargetManager->Release();
	}
	RenderDevice.OnResize(W, H);
}

bool FRenderer::RenderScreenUIPass(const FScreenUIPassInputs& PassInputs,
                                   const FFrameContext&       Frame,
                                   ID3D11RenderTargetView*    RenderTargetView,
                                   ID3D11DepthStencilView*    DepthStencilView)
{
	return ScreenUIRenderer && ScreenUIRenderer->Render(*this, Frame, PassInputs, RenderTargetView, DepthStencilView);
}

bool FRenderer::ComposeViewports(const FViewportCompositePassInputs& Inputs,
                                 const FFrameContext&                Frame,
                                 const FViewContext&                 View,
                                 ID3D11RenderTargetView*             RenderTargetView,
                                 ID3D11DepthStencilView*             DepthStencilView)
{
	if (!RenderTargetView)
	{
		return false;
	}

	return ViewportCompositor &&
			ViewportCompositor->Compose(*this, Frame, View, RenderTargetView, DepthStencilView, Inputs);
}

bool FRenderer::RenderGameFrame(const FGameFrameRequest& Request)
{
	return FGameFrameRenderer::Render(*this, Request);
}

bool FRenderer::RenderEditorFrame(const FEditorFrameRequest& Request)
{
	return FEditorFrameRenderer::Render(*this, Request);
}

bool FRenderer::CreateTextureFromSTB(
	ID3D11Device*              Device,
	const char*               FilePath,
	ID3D11ShaderResourceView** OutSRV,
	ETextureColorSpace        ColorSpace)
{
	if (FilePath == nullptr)
	{
		return false;
	}

	return CreateTextureFromSTB(Device, FPaths::ToPath(FilePath), OutSRV, ColorSpace);
}

bool FRenderer::CreateTextureFromSTB(
	ID3D11Device*                Device,
	const std::filesystem::path& FilePath,
	ID3D11ShaderResourceView**   OutSRV,
	ETextureColorSpace           ColorSpace)
{
	if (Device == nullptr || OutSRV == nullptr || FilePath.empty())
	{
		return false;
	}

	std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
	if (!File.is_open())
	{
		return false;
	}

	const std::streamsize FileSize = File.tellg();
	if (FileSize <= 0)
	{
		return false;
	}

	File.seekg(0, std::ios::beg);
	std::vector<unsigned char> FileBytes(static_cast<size_t>(FileSize));
	if (!File.read(reinterpret_cast<char*>(FileBytes.data()), FileSize))
	{
		return false;
	}

	int            W    = 0;
	int            H    = 0;
	int            C    = 0;
	unsigned char* Data = stbi_load_from_memory(FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
	if (!Data)
	{
		return false;
	}

	const bool   bSRGB        = (ColorSpace == ETextureColorSpace::SRGB);
	const DXGI_FORMAT TextureFormat = bSRGB ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT SRVFormat     = bSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width                = W;
	Desc.Height               = H;
	Desc.MipLevels            = 1;
	Desc.ArraySize            = 1;
	Desc.Format               = TextureFormat;
	Desc.SampleDesc.Count     = 1;
	Desc.Usage                = D3D11_USAGE_DEFAULT;
	Desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitData = {Data, static_cast<UINT>(W * 4), 0};
	ID3D11Texture2D*       Tex      = nullptr;
	HRESULT                Hr       = Device->CreateTexture2D(&Desc, &InitData, &Tex);
	stbi_image_free(Data);
	if (FAILED(Hr))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = SRVFormat;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels             = 1;
	Hr                                      = Device->CreateShaderResourceView(Tex, &SRVDesc, OutSRV);
	Tex->Release();
	return SUCCEEDED(Hr);
}

void FRenderer::ConfigureMaterialPasses(FMaterial& Material, bool bTexturedMaterial)
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	const std::wstring   ShaderDir = FPaths::ShaderDir();
	FMaterialPassShaders DepthPass;
	FMaterialPassShaders GBufferPass;
	FMaterialPassShaders OutlineMaskPass;
	if (bTexturedMaterial)
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/DepthOnlyTextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/TextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SceneGeometry/GBufferTexturePixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS = FShaderMap::Get().GetOrCreatePixelShader(
			Device,
			(ShaderDir + L"SelectionHighlight/OutlineMaskTexturePixelShader.hlsl").c_str());
	}
	else
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/DepthOnlyVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(Device,
		                                                           (ShaderDir + L"SceneGeometry/VertexShader.hlsl").c_str(),
		                                                           EVertexLayoutType::MeshVertex);
		GBufferPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SceneGeometry/GBufferColorPixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SelectionHighlight/OutlineMaskPixelShader.hlsl").c_str());
	}

	Material.SetPassShaders(EMaterialPassType::DepthOnly, DepthPass);
	Material.SetPassShaders(EMaterialPassType::GBuffer, GBufferPass);
	Material.SetPassShaders(EMaterialPassType::OutlineMask, OutlineMaskPass);
}

size_t FRenderer::GetPrevCommandCount() const
{
	return SceneRenderer ? SceneRenderer->GetPrevCommandCount() : 0;
}

ISceneTextFeature* FRenderer::GetSceneTextFeature() const
{
	return TextFeature.get();
}

ISceneSubUVFeature* FRenderer::GetSceneSubUVFeature() const
{
	return SubUVFeature.get();
}

ISceneBillboardFeature* FRenderer::GetSceneBillboardFeature() const
{
	return BillboardFeature.get();
}

FFogRenderFeature* FRenderer::GetFogFeature() const
{
	return FogFeature.get();
}

FOutlineRenderFeature* FRenderer::GetOutlineFeature() const
{
	return OutlineFeature.get();
}

FDebugLineRenderFeature* FRenderer::GetDebugLineFeature() const
{
	return DebugLineFeature.get();
}

FDecalRenderFeature* FRenderer::GetDecalFeature() const
{
	return DecalFeature.get();
}

FVolumeDecalRenderFeature* FRenderer::GetVolumeDecalFeature() const
{
	return VolumeDecalFeature.get();
}

FFireBallRenderFeature* FRenderer::GetFireBallFeature() const
{
	return FireBallFeature.get();
}

FFXAARenderFeature* FRenderer::GetFXAAFeature() const
{
	return FXAAFeature.get();
}

FToneMappingRenderFeature* FRenderer::GetToneMappingRenderFeature() const
{
	return FToneMappingFeature.get();
}

FBillboardRenderer& FRenderer::GetBillboardRenderer()
{
	return BillboardFeature->GetRenderer();
}

const FDecalFrameStats& FRenderer::GetDecalFrameStats() const
{
	static constexpr FDecalFrameStats EmptyStats = {};
	return DecalFeature ? DecalFeature->GetFrameStats() : EmptyStats;
}

FMeshPassFrameStats FRenderer::GetMeshPassFrameStats() const
{
	static constexpr FMeshPassFrameStats EmptyStats = {};
	return SceneRenderer ? SceneRenderer->GetMeshPassFrameStats() : EmptyStats;
}

FDecalStats FRenderer::GetDecalStats() const
{
	FDecalStats Stats;
	Stats.Common.Mode = GetDecalProjectionMode();

	if (Stats.Common.Mode == EDecalProjectionMode::VolumeDraw)
	{
		if (VolumeDecalFeature)
		{
			Stats.Volume                        = VolumeDecalFeature->GetStats();
			Stats.Common.BuildTimeMs            = VolumeDecalFeature->GetBuildTimeMs();
			Stats.Common.CullIntersectionTimeMs = VolumeDecalFeature->GetCullIntersectionTimeMs();
			Stats.Common.ShadingPassTimeMs      = VolumeDecalFeature->GetShadingPassTimeMs();
			Stats.Common.TotalDecalTimeMs       = VolumeDecalFeature->GetTotalTimeMs();
		}
		return Stats;
	}

	if (DecalFeature)
	{
		Stats.ClusteredLookup               = DecalFeature->GetClusteredStats();
		const FDecalFrameStats& FrameStats  = DecalFeature->GetFrameStats();
		Stats.Common.BuildTimeMs            = FrameStats.VisibleBuildTimeMs;
		Stats.Common.CullIntersectionTimeMs = FrameStats.ClusterBuildTimeMs;
		Stats.Common.ShadingPassTimeMs      = FrameStats.ShadingPassTimeMs;
		Stats.Common.TotalDecalTimeMs       = FrameStats.TotalDecalTimeMs;
	}

	return Stats;
}

FFogStats FRenderer::GetFogStats() const
{
	FFogStats Stats;
	if (FogFeature)
	{
		Stats = FogFeature->GetStats();
	}
	return Stats;
}

FGPUFrameStats FRenderer::GetGPUStats() const
{
	FGPUFrameStats            Stats;
	const FDecalStats         DecalStats      = GetDecalStats();
	const FFogStats           FogStats        = GetFogStats();
	const FMeshPassFrameStats MeshPassStats   = GetMeshPassFrameStats();
	const FDecalFrameStats&   DecalFrameStats = GetDecalFrameStats();
	const bool                bHasDecalPass   =
			(DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
				? (DecalStats.Volume.DecalDrawCalls > 0u)
				: (DecalStats.ClusteredLookup.UploadedDecalCount > 0u && DecalStats.ClusteredLookup.DecalCellRegistrations > 0u);

	Stats.GeometryDrawCalls = MeshPassStats.TotalDrawCalls;
	Stats.DecalDrawCalls    = (DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
		                       ? DecalStats.Volume.DecalDrawCalls
		                       : (bHasDecalPass ? 1u : 0u);
	Stats.FogDrawCalls          = FogStats.Common.DrawCallCount;
	Stats.GeometryTimeMs        = MeshPassStats.TotalTimeMs;
	Stats.PixelShadingTimeMs    = DecalStats.Common.ShadingPassTimeMs + FogStats.Common.ShadingPassTimeMs;
	Stats.MemoryBandwidthTimeMs = DecalFrameStats.UploadDecalBufferTimeMs
			+ DecalFrameStats.UploadClusterHeaderBufferTimeMs
			+ DecalFrameStats.UploadClusterIndexBufferTimeMs
			+ FogStats.Common.StructuredBufferUploadTimeMs;
	Stats.OverdrawFillrateTimeMs = Stats.PixelShadingTimeMs;

	Stats.UploadBytes = static_cast<uint64>(DecalFrameStats.UploadedDecalCount) * sizeof(FDecalGPUData)
			+ static_cast<uint64>(DecalFrameStats.UploadedClusterHeaderCount) * sizeof(FDecalClusterHeaderGPU)
			+ static_cast<uint64>(DecalFrameStats.UploadedClusterIndexCount) * sizeof(uint32)
			+ FogStats.Common.TotalUploadBytes;
	Stats.CopyBytes = FogStats.Common.SceneColorCopyBytes;

	Stats.FullscreenPassCount = 0;
	if (DecalStats.Common.Mode == EDecalProjectionMode::ClusteredLookup && bHasDecalPass)
	{
		++Stats.FullscreenPassCount;
	}
	Stats.FullscreenPassCount += FogStats.Common.FullscreenPassCount;

	Stats.PassCount = 4; // clear, upload, depth prepass, forward opaque
	if (bHasDecalPass)
	{
		++Stats.PassCount;
	}
	if (FogStats.Common.TotalFogVolumes > 0u)
	{
		++Stats.PassCount;
	}
	Stats.PassCount += 1; // fireball
	Stats.PassCount += 1; // forward transparent
	Stats.PassCount += 1; // editor grid
	Stats.PassCount += 2; // outline
	Stats.PassCount += 1; // fxaa
	Stats.PassCount += 1; // ToneMapping
	Stats.PassCount += 2; // editor overlay passes

	Stats.DrawCallCount             = Stats.GeometryDrawCalls + Stats.DecalDrawCalls + Stats.FogDrawCalls;
	Stats.EstimatedFullscreenPixels = static_cast<uint64>(
				FogStats.Common.FullscreenPassCount +
				((DecalStats.Common.Mode == EDecalProjectionMode::ClusteredLookup && bHasDecalPass) ? 1u : 0u))
			* static_cast<uint64>(GetBackBufferViewport().Width)
			* static_cast<uint64>(GetBackBufferViewport().Height);

	return Stats;
}

void FRenderer::SetConstantBuffers()
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	ID3D11Buffer* CBs[2] = {FrameConstantBuffer, ObjectConstantBuffer};
	DeviceContext->VSSetConstantBuffers(0, 2, CBs);
	DeviceContext->PSSetConstantBuffers(0, 2, CBs);
	DeviceContext->CSSetConstantBuffers(0, 2, CBs);
}

void FRenderer::UpdateFrameConstantBuffer(const FFrameContext& Frame, const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FFrameConstantBuffer CBData;
	CBData.View       = View.View.GetTransposed();
	CBData.Projection = View.Projection.GetTransposed();
	CBData.Time       = Frame.TotalTimeSeconds;
	CBData.DeltaTime  = Frame.DeltaTimeSeconds;
	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(FrameConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FrameConstantBuffer, 0);
	}
}

void FRenderer::UpdateObjectConstantBuffer(const FMatrix& WorldMatrix)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FObjectConstantBuffer CBData;
	CBData.World = WorldMatrix.GetTransposed();
	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(ObjectConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(ObjectConstantBuffer, 0);
	}
}

void FRenderer::ClearDepthBuffer(ID3D11DepthStencilView* DepthStencilView)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext || !DepthStencilView)
	{
		return;
	}

	DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void FRenderer::PreparePassDomain(EPassDomain Domain, const FSceneRenderTargets& Targets)
{
	if (!RenderStateManager)
	{
		return;
	}

	switch (Domain)
	{
	case EPassDomain::Compute:
		RenderStateManager->ClearAllGraphicsState();
		RenderStateManager->ClearAllComputeState();
		if (Targets.GetSceneColorShaderResource())
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.GetSceneColorTexture());
		}
		if (Targets.GetSceneColorWriteTexture())
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.GetSceneColorWriteTexture());
		}
		break;

	case EPassDomain::Copy:
		RenderStateManager->ClearAllGraphicsState();
		RenderStateManager->ClearAllComputeState();
		break;

	case EPassDomain::Graphics:
	default:
		RenderStateManager->ClearAllComputeState();
		break;
	}
}

bool FRenderer::ResolveSceneColorTargets(const FSceneRenderTargets& Targets)
{
	if (!Targets.NeedsSceneColorResolve())
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = GetDeviceContext();
	if (!DeviceContext
		|| !Targets.SceneColorRead
		|| !Targets.SceneColorRead->Texture
		|| !Targets.SceneColorRead->SRV
		|| !Targets.FinalSceneColor
		|| !Targets.FinalSceneColor->RTV
		|| !SceneColorResolveVertexShader
		|| !SceneColorResolvePixelShader
		|| !SceneColorResolveSampler)
	{
		return false;
	}

	ID3D11Texture2D* DestinationTexture = Targets.FinalSceneColor->Texture;
	if (!DestinationTexture)
	{
		DestinationTexture = ResolveTextureFromRenderTarget(Targets.FinalSceneColor->RTV);
	}

	if (RenderStateManager)
	{
		RenderStateManager->UnbindResourceEverywhere(Targets.SceneColorRead->Texture);
		if (DestinationTexture)
		{
			RenderStateManager->UnbindResourceEverywhere(DestinationTexture);
		}
	}

	D3D11_VIEWPORT ResolveViewport = {};
	ResolveViewport.TopLeftX       = 0.0f;
	ResolveViewport.TopLeftY       = 0.0f;
	ResolveViewport.Width          = static_cast<float>(Targets.Width);
	ResolveViewport.Height         = static_cast<float>(Targets.Height);
	ResolveViewport.MinDepth       = 0.0f;
	ResolveViewport.MaxDepth       = 1.0f;

	constexpr float         BlendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	ID3D11RenderTargetView* ResolveRTV     = Targets.FinalSceneColor->RTV;

	DeviceContext->OMSetRenderTargets(1, &ResolveRTV, nullptr);
	DeviceContext->RSSetViewports(1, &ResolveViewport);
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffffu);
	DeviceContext->OMSetDepthStencilState(nullptr, 0);
	DeviceContext->RSSetState(nullptr);

	DeviceContext->IASetInputLayout(nullptr);
	DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DeviceContext->VSSetShader(SceneColorResolveVertexShader, nullptr, 0);
	DeviceContext->PSSetShader(SceneColorResolvePixelShader, nullptr, 0);

	ID3D11ShaderResourceView* SceneColorSRV = Targets.SceneColorRead->SRV;
	DeviceContext->PSSetShaderResources(0, 1, &SceneColorSRV);
	DeviceContext->PSSetSamplers(0, 1, &SceneColorResolveSampler);

	DeviceContext->Draw(3, 0);

	ID3D11ShaderResourceView* NullSRV     = nullptr;
	ID3D11SamplerState*       NullSampler = nullptr;
	DeviceContext->PSSetShaderResources(0, 1, &NullSRV);
	DeviceContext->PSSetSamplers(0, 1, &NullSampler);
	DeviceContext->VSSetShader(nullptr, nullptr, 0);
	DeviceContext->PSSetShader(nullptr, nullptr, 0);

	if (!Targets.FinalSceneColor->Texture && DestinationTexture)
	{
		DestinationTexture->Release();
	}

	return true;
}

bool FRenderer::CreateConstantBuffers()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage             = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
	Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

	Desc.ByteWidth = sizeof(FFrameConstantBuffer);
	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FrameConstantBuffer)))
	{
		return false;
	}

	Desc.ByteWidth = sizeof(FObjectConstantBuffer);
	return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &ObjectConstantBuffer));
}

bool FRenderer::CreateSamplers()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD             = 0;
	SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

	HRESULT Hr = Device->CreateSamplerState(&SamplerDesc, &NormalSampler);
	if (FAILED(Hr))
	{
		return false;
	}

	return true;
}

bool FRenderer::CreateSceneColorResolveResources()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	auto BlitVSResource = FShaderResource::GetOrCompile(
		(ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl").c_str(),
		"main",
		"vs_5_0");
	if (!BlitVSResource
		|| FAILED(Device->CreateVertexShader(
			BlitVSResource->GetBufferPointer(),
			BlitVSResource->GetBufferSize(),
			nullptr,
			&SceneColorResolveVertexShader)))
	{
		ReleaseSceneColorResolveResources();
		return false;
	}

	auto BlitPSResource = FShaderResource::GetOrCompile(
		(ShaderDir + L"FinalImagePostProcess/BlitPixelShader.hlsl").c_str(),
		"main",
		"ps_5_0");
	if (!BlitPSResource
		|| FAILED(Device->CreatePixelShader(
			BlitPSResource->GetBufferPointer(),
			BlitPSResource->GetBufferSize(),
			nullptr,
			&SceneColorResolvePixelShader)))
	{
		ReleaseSceneColorResolveResources();
		return false;
	}

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD             = 0.0f;
	SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

	if (FAILED(Device->CreateSamplerState(&SamplerDesc, &SceneColorResolveSampler)))
	{
		ReleaseSceneColorResolveResources();
		return false;
	}

	return true;
}

void FRenderer::ReleaseSceneColorResolveResources()
{
	if (SceneColorResolveSampler)
	{
		SceneColorResolveSampler->Release();
		SceneColorResolveSampler = nullptr;
	}

	if (SceneColorResolvePixelShader)
	{
		SceneColorResolvePixelShader->Release();
		SceneColorResolvePixelShader = nullptr;
	}

	if (SceneColorResolveVertexShader)
	{
		SceneColorResolveVertexShader->Release();
		SceneColorResolveVertexShader = nullptr;
	}
}
