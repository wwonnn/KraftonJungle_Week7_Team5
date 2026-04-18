#include "Renderer/Frame/SceneTargetManager.h"

#include <array>

namespace
{
	void ReleaseCOMResource(IUnknown*& Resource)
	{
		if (Resource)
		{
			Resource->Release();
			Resource = nullptr;
		}
	}
}

bool FSceneTargetManager::EnsureGameSceneTargets(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0 || !Device)
	{
		return false;
	}

	if (GameSceneDepth.DSV
		&& GameSceneTargetCacheWidth == Width
		&& GameSceneTargetCacheHeight == Height)
	{
		return EnsureSupplementalTargets(Device, Width, Height);
	}

	Release();

	if (!CreateDepthTexture(Device, Width, Height, GameSceneDepth))
	{
		Release();
		return false;
	}

	GameSceneTargetCacheWidth  = Width;
	GameSceneTargetCacheHeight = Height;
	return EnsureSupplementalTargets(Device, Width, Height);
}

bool FSceneTargetManager::EnsureSupplementalTargets(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0 || !Device)
	{
		return false;
	}

	if (InternalSceneColorA.RTV
		&& InternalSceneColorB.RTV
		&& GBufferASurface.RTV
		&& GBufferBSurface.RTV
		&& GBufferCSurface.RTV
		&& OverlayColorSurface.RTV
		&& OutlineMaskSurface.RTV
		&& SupplementalTargetCacheWidth == Width
		&& SupplementalTargetCacheHeight == Height)
	{
		return true;
	}

	ReleaseSupplementalTargets();

	if (!CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, InternalSceneColorA, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, InternalSceneColorB, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, GBufferASurface, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, GBufferBSurface, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, GBufferCSurface, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, OverlayColorSurface, true)
		|| !CreateColorTexture(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, OutlineMaskSurface, true))
	{
		Release();
		return false;
	}

	SupplementalTargetCacheWidth  = Width;
	SupplementalTargetCacheHeight = Height;
	return true;
}

bool FSceneTargetManager::AcquireGameSceneTargets(ID3D11Device* Device, const D3D11_VIEWPORT& Viewport, FSceneRenderTargets& OutTargets)
{
	if (!EnsureGameSceneTargets(Device, static_cast<uint32>(Viewport.Width), static_cast<uint32>(Viewport.Height)))
	{
		return false;
	}

	OutTargets                 = {};
	OutTargets.Width           = static_cast<uint32>(Viewport.Width);
	OutTargets.Height          = static_cast<uint32>(Viewport.Height);
	OutTargets.FinalSceneColor = nullptr;
	OutTargets.SceneColorRead  = &InternalSceneColorA;
	OutTargets.SceneColorWrite = &InternalSceneColorB;
	OutTargets.OverlayColor    = &OverlayColorSurface;
	OutTargets.SceneDepth      = &GameSceneDepth;
	OutTargets.GBufferA        = &GBufferASurface;
	OutTargets.GBufferB        = &GBufferBSurface;
	OutTargets.GBufferC        = &GBufferCSurface;
	OutTargets.OutlineMask     = &OutlineMaskSurface;
	OutTargets.RefreshCompatibilityViews();
	return true;
}

bool FSceneTargetManager::WrapExternalSceneTargets(
	ID3D11Device*             Device,
	ID3D11RenderTargetView*   RenderTargetView,
	ID3D11ShaderResourceView* RenderTargetShaderResourceView,
	ID3D11DepthStencilView*   DepthStencilView,
	ID3D11ShaderResourceView* DepthShaderResourceView,
	const D3D11_VIEWPORT&     Viewport,
	FSceneRenderTargets&      OutTargets)
{
	const uint32 Width  = static_cast<uint32>(Viewport.Width);
	const uint32 Height = static_cast<uint32>(Viewport.Height);
	if (!RenderTargetView || !RenderTargetShaderResourceView || !DepthStencilView || !DepthShaderResourceView
		|| !EnsureSupplementalTargets(Device, Width, Height))
	{
		return false;
	}

	FExternalOverlayTargets* ExternalOverlayTargets = nullptr;
	if (!EnsureExternalOverlayTargets(
		Device,
		RenderTargetView,
		DepthStencilView,
		Width,
		Height,
		ExternalOverlayTargets))
	{
		return false;
	}

	WrapExternalColorTarget(Width, Height, RenderTargetView, RenderTargetShaderResourceView, WrappedFinalSceneColor);
	WrapExternalDepthTarget(Width, Height, DepthStencilView, DepthShaderResourceView, WrappedSceneDepth);

	OutTargets                 = {};
	OutTargets.Width           = Width;
	OutTargets.Height          = Height;
	OutTargets.FinalSceneColor = &WrappedFinalSceneColor;
	OutTargets.SceneColorRead  = &InternalSceneColorA;
	OutTargets.SceneColorWrite = &InternalSceneColorB;
	OutTargets.OverlayColor    = &ExternalOverlayTargets->OverlayColor;
	OutTargets.SceneDepth      = &WrappedSceneDepth;
	OutTargets.GBufferA        = &GBufferASurface;
	OutTargets.GBufferB        = &GBufferBSurface;
	OutTargets.GBufferC        = &GBufferCSurface;
	OutTargets.OutlineMask     = &OutlineMaskSurface;
	OutTargets.RefreshCompatibilityViews();
	return true;
}

void FSceneTargetManager::Release()
{
	ReleaseTexture(GameSceneDepth);
	GameSceneTargetCacheWidth  = 0;
	GameSceneTargetCacheHeight = 0;

	ReleaseSupplementalTargets();
	ReleaseWrappedExternalTargets();

	for (auto& It : ExternalOverlayTargetMap)
	{
		ReleaseExternalOverlayTargets(It.second);
	}
	ExternalOverlayTargetMap.clear();
}

bool FSceneTargetManager::CreateColorTexture(
	ID3D11Device*  Device,
	uint32         Width,
	uint32         Height,
	DXGI_FORMAT    Format,
	FGPUTexture2D& OutTexture,
	bool           bCreateUAV,
	uint32         MipLevels)
{
	return CreateColorTexture(
		Device,
		Width,
		Height,
		Format,
		Format,
		Format,
		Format,
		OutTexture,
		bCreateUAV,
		MipLevels
	);
}

bool FSceneTargetManager::CreateColorTexture(
	ID3D11Device*  Device,
	uint32         Width,
	uint32         Height,
	DXGI_FORMAT    TextureFormat,
	DXGI_FORMAT    SRVFormat,
	DXGI_FORMAT    RTVFormat,
	DXGI_FORMAT    UAVFormat,
	FGPUTexture2D& OutTexture,
	bool           bCreateUAV,
	uint32         MipLevels)
{
	if (!Device || Width == 0 || Height == 0)
	{
		return false;
	}

	ReleaseTexture(OutTexture);

	OutTexture.Desc               = {};
	OutTexture.Desc.Width         = Width;
	OutTexture.Desc.Height        = Height;
	OutTexture.Desc.MipLevels     = MipLevels;
	OutTexture.Desc.TextureFormat = TextureFormat;
	OutTexture.Desc.SRVFormat     = SRVFormat;
	OutTexture.Desc.RTVFormat     = RTVFormat;
	OutTexture.Desc.UAVFormat     = UAVFormat;
	OutTexture.Desc.BindFlags     = ETextureBindFlags::SRV | ETextureBindFlags::RTV;
	if (bCreateUAV)
	{
		OutTexture.Desc.BindFlags |= ETextureBindFlags::UAV;
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width                = Width;
	Desc.Height               = Height;
	Desc.MipLevels            = MipLevels;
	Desc.ArraySize            = 1;
	Desc.Format               = TextureFormat;
	Desc.SampleDesc.Count     = 1;
	Desc.Usage                = D3D11_USAGE_DEFAULT;
	Desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (bCreateUAV)
	{
		Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &OutTexture.Texture)) || !OutTexture.Texture)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	RTVDesc.Format                        = RTVFormat;
	RTVDesc.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Texture2D.MipSlice            = 0;
	if (FAILED(Device->CreateRenderTargetView(OutTexture.Texture, &RTVDesc, &OutTexture.RTV)) || !OutTexture.RTV)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = SRVFormat;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels             = MipLevels;
	SRVDesc.Texture2D.MostDetailedMip       = 0;
	if (FAILED(Device->CreateShaderResourceView(OutTexture.Texture, &SRVDesc, &OutTexture.SRV)) || !OutTexture.SRV)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	if (bCreateUAV)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.Format                           = UAVFormat;
		UAVDesc.ViewDimension                    = D3D11_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice               = 0;
		if (FAILED(Device->CreateUnorderedAccessView(OutTexture.Texture, &UAVDesc, &OutTexture.UAV)) || !OutTexture.UAV)
		{
			ReleaseTexture(OutTexture);
			return false;
		}
	}

	if (MipLevels > 1)
	{
		OutTexture.MipSRVs.resize(MipLevels, nullptr);
		if (bCreateUAV)
		{
			OutTexture.MipUAVs.resize(MipLevels, nullptr);
		}
		for (uint32 MipIndex = 0; MipIndex < MipLevels; ++MipIndex)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC MipSRVDesc = SRVDesc;
			MipSRVDesc.Texture2D.MostDetailedMip       = MipIndex;
			MipSRVDesc.Texture2D.MipLevels             = 1;
			if (FAILED(Device->CreateShaderResourceView(OutTexture.Texture, &MipSRVDesc, &OutTexture.MipSRVs[MipIndex])))
			{
				ReleaseTexture(OutTexture);
				return false;
			}
			if (bCreateUAV)
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC MipUAVDesc = {};
				MipUAVDesc.Format                           = OutTexture.Desc.UAVFormat;
				MipUAVDesc.ViewDimension                    = D3D11_UAV_DIMENSION_TEXTURE2D;
				MipUAVDesc.Texture2D.MipSlice               = MipIndex;
				if (FAILED(Device->CreateUnorderedAccessView(OutTexture.Texture, &MipUAVDesc, &OutTexture.MipUAVs[MipIndex])))
				{
					ReleaseTexture(OutTexture);
					return false;
				}
			}
		}
	}
	return true;
}


bool FSceneTargetManager::CreateDepthTexture(
	ID3D11Device*  Device,
	uint32         Width,
	uint32         Height,
	FGPUTexture2D& OutTexture)
{
	if (!Device || Width == 0 || Height == 0)
	{
		return false;
	}

	ReleaseTexture(OutTexture);

	OutTexture.Desc               = {};
	OutTexture.Desc.Width         = Width;
	OutTexture.Desc.Height        = Height;
	OutTexture.Desc.TextureFormat = DXGI_FORMAT_R24G8_TYPELESS;
	OutTexture.Desc.SRVFormat     = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	OutTexture.Desc.DSVFormat     = DXGI_FORMAT_D24_UNORM_S8_UINT;
	OutTexture.Desc.BindFlags     = ETextureBindFlags::SRV | ETextureBindFlags::DSV;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width                = Width;
	Desc.Height               = Height;
	Desc.MipLevels            = 1;
	Desc.ArraySize            = 1;
	Desc.Format               = OutTexture.Desc.TextureFormat;
	Desc.SampleDesc.Count     = 1;
	Desc.Usage                = D3D11_USAGE_DEFAULT;
	Desc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &OutTexture.Texture)) || !OutTexture.Texture)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format                        = OutTexture.Desc.DSVFormat;
	DSVDesc.ViewDimension                 = D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(Device->CreateDepthStencilView(OutTexture.Texture, &DSVDesc, &OutTexture.DSV)) || !OutTexture.DSV)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = OutTexture.Desc.SRVFormat;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels             = 1;
	if (FAILED(Device->CreateShaderResourceView(OutTexture.Texture, &SRVDesc, &OutTexture.SRV)) || !OutTexture.SRV)
	{
		ReleaseTexture(OutTexture);
		return false;
	}

	return true;
}

void FSceneTargetManager::WrapExternalColorTarget(
	uint32                    Width,
	uint32                    Height,
	ID3D11RenderTargetView*   RenderTargetView,
	ID3D11ShaderResourceView* ShaderResourceView,
	FGPUTexture2D&            OutTexture)
{
	ReleaseTexture(OutTexture);
	OutTexture.Desc                  = {};
	OutTexture.Desc.Width            = Width;
	OutTexture.Desc.Height           = Height;
	OutTexture.Desc.BindFlags        = ETextureBindFlags::SRV | ETextureBindFlags::RTV;
	OutTexture.Desc.bExternalWrapped = true;

	if (RenderTargetView)
	{
		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
		RenderTargetView->GetDesc(&RTVDesc);
		OutTexture.Desc.RTVFormat = RTVDesc.Format;

		ID3D11Resource* Resource = nullptr;
		RenderTargetView->GetResource(&Resource);
		if (Resource)
		{
			ID3D11Texture2D* Texture = nullptr;
			if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture))) && Texture)
			{
				D3D11_TEXTURE2D_DESC TextureDesc = {};
				Texture->GetDesc(&TextureDesc);
				OutTexture.Desc.TextureFormat = TextureDesc.Format;
				Texture->Release();
			}
			Resource->Release();
		}
	}

	if (ShaderResourceView)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		ShaderResourceView->GetDesc(&SRVDesc);
		OutTexture.Desc.SRVFormat = SRVDesc.Format;
	}

	OutTexture.RTV = RenderTargetView;
	OutTexture.SRV = ShaderResourceView;
}

void FSceneTargetManager::WrapExternalDepthTarget(
	uint32                    Width,
	uint32                    Height,
	ID3D11DepthStencilView*   DepthStencilView,
	ID3D11ShaderResourceView* ShaderResourceView,
	FGPUTexture2D&            OutTexture)
{
	ReleaseTexture(OutTexture);
	OutTexture.Desc                  = {};
	OutTexture.Desc.Width            = Width;
	OutTexture.Desc.Height           = Height;
	OutTexture.Desc.BindFlags        = ETextureBindFlags::SRV | ETextureBindFlags::DSV;
	OutTexture.Desc.bExternalWrapped = true;
	OutTexture.DSV                   = DepthStencilView;
	OutTexture.SRV                   = ShaderResourceView;
}

void FSceneTargetManager::ReleaseTexture(FGPUTexture2D& Texture)
{
	for (ID3D11ShaderResourceView*& MipSRV : Texture.MipSRVs)
	{
		ReleaseCOM(reinterpret_cast<IUnknown*&>(MipSRV));
	}
	Texture.MipSRVs.clear();

	for (ID3D11UnorderedAccessView*& MipUAV : Texture.MipUAVs)
	{
		ReleaseCOM(reinterpret_cast<IUnknown*&>(MipUAV));
	}
	Texture.MipUAVs.clear();

	if (!Texture.IsExternal())
	{
		ReleaseCOM(reinterpret_cast<IUnknown*&>(Texture.UAV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(Texture.DSV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(Texture.SRV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(Texture.RTV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(Texture.Texture));
	}
	else
	{
		Texture.UAV     = nullptr;
		Texture.DSV     = nullptr;
		Texture.SRV     = nullptr;
		Texture.RTV     = nullptr;
		Texture.Texture = nullptr;
	}

	Texture.Desc = {};
}

void FSceneTargetManager::ReleaseCOM(IUnknown*& Resource)
{
	ReleaseCOMResource(Resource);
}

uint64 FSceneTargetManager::MakeExternalOverlayKey(
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView)
{
	const uint64 A = reinterpret_cast<uint64>(RenderTargetView);
	const uint64 B = reinterpret_cast<uint64>(DepthStencilView);
	return A ^ (B + 0x9e3779b97f4a7c15ull + (A << 6) + (A >> 2));
}

bool FSceneTargetManager::EnsureExternalOverlayTargets(
	ID3D11Device*             Device,
	ID3D11RenderTargetView*   RenderTargetView,
	ID3D11DepthStencilView*   DepthStencilView,
	uint32                    Width,
	uint32                    Height,
	FExternalOverlayTargets*& OutTargets)
{
	OutTargets = nullptr;

	if (!Device || !RenderTargetView || !DepthStencilView || Width == 0 || Height == 0)
	{
		return false;
	}

	const uint64             Key   = MakeExternalOverlayKey(RenderTargetView, DepthStencilView);
	FExternalOverlayTargets& Entry = ExternalOverlayTargetMap[Key];

	if (Entry.OverlayColor.RTV && Entry.Width == Width && Entry.Height == Height)
	{
		OutTargets = &Entry;
		return true;
	}

	ReleaseExternalOverlayTargets(Entry);

	if (!CreateColorTexture(
		Device,
		Width,
		Height,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		Entry.OverlayColor,
		true))
	{
		ExternalOverlayTargetMap.erase(Key);
		return false;
	}

	Entry.Width  = Width;
	Entry.Height = Height;
	OutTargets   = &Entry;
	return true;
}

void FSceneTargetManager::ReleaseExternalOverlayTargets(FExternalOverlayTargets& Targets)
{
	ReleaseTexture(Targets.OverlayColor);
	Targets.Width  = 0;
	Targets.Height = 0;
}

void FSceneTargetManager::ReleaseSupplementalTargets()
{
	ReleaseTexture(InternalSceneColorA);
	ReleaseTexture(InternalSceneColorB);
	ReleaseTexture(GBufferASurface);
	ReleaseTexture(GBufferBSurface);
	ReleaseTexture(GBufferCSurface);
	ReleaseTexture(OverlayColorSurface);
	ReleaseTexture(OutlineMaskSurface);
	SupplementalTargetCacheWidth  = 0;
	SupplementalTargetCacheHeight = 0;
}

void FSceneTargetManager::ReleaseWrappedExternalTargets()
{
	ReleaseTexture(WrappedFinalSceneColor);
	ReleaseTexture(WrappedSceneDepth);
}
