#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>
#include <unordered_map>

class ENGINE_API FSceneTargetManager
{
public:
	bool EnsureGameSceneTargets(ID3D11Device* Device, uint32 Width, uint32 Height);
	bool EnsureSupplementalTargets(ID3D11Device* Device, uint32 Width, uint32 Height);
	bool AcquireGameSceneTargets(ID3D11Device* Device, const D3D11_VIEWPORT& Viewport, FSceneRenderTargets& OutTargets);
	bool WrapExternalSceneTargets(
		ID3D11Device*             Device,
		ID3D11RenderTargetView*   RenderTargetView,
		ID3D11ShaderResourceView* RenderTargetShaderResourceView,
		ID3D11DepthStencilView*   DepthStencilView,
		ID3D11ShaderResourceView* DepthShaderResourceView,
		const D3D11_VIEWPORT&     Viewport,
		FSceneRenderTargets&      OutTargets);
	void Release();

private:
	static bool CreateColorTexture(
		ID3D11Device*  Device,
		uint32         Width,
		uint32         Height,
		DXGI_FORMAT    Format,
		FGPUTexture2D& OutTexture,
		bool           bCreateUAV = false,
		uint32         MipLevels  = 1);
	static bool CreateColorTexture(
		ID3D11Device*  Device,
		uint32         Width,
		uint32         Height,
		DXGI_FORMAT    TextureFormat,
		DXGI_FORMAT    SRVFormat,
		DXGI_FORMAT    RTVFormat,
		DXGI_FORMAT    UAVFormat,
		FGPUTexture2D& OutTexture,
		bool           bCreateUAV = false,
		uint32         MipLevels  = 1);
	static bool CreateDepthTexture(
		ID3D11Device*  Device,
		uint32         Width,
		uint32         Height,
		FGPUTexture2D& OutTexture);
	static void WrapExternalColorTarget(
		uint32                    Width,
		uint32                    Height,
		ID3D11RenderTargetView*   RenderTargetView,
		ID3D11ShaderResourceView* ShaderResourceView,
		FGPUTexture2D&            OutTexture);
	static void WrapExternalDepthTarget(
		uint32                    Width,
		uint32                    Height,
		ID3D11DepthStencilView*   DepthStencilView,
		ID3D11ShaderResourceView* ShaderResourceView,
		FGPUTexture2D&            OutTexture);
	static void   ReleaseTexture(FGPUTexture2D& Texture);
	static void   ReleaseCOM(IUnknown*& Resource);
	static uint64 MakeExternalOverlayKey(
		ID3D11RenderTargetView* RenderTargetView,
		ID3D11DepthStencilView* DepthStencilView);

	struct FExternalOverlayTargets
	{
		uint32        Width  = 0;
		uint32        Height = 0;
		FGPUTexture2D OverlayColor;
	};

	bool EnsureExternalOverlayTargets(
		ID3D11Device*             Device,
		ID3D11RenderTargetView*   RenderTargetView,
		ID3D11DepthStencilView*   DepthStencilView,
		uint32                    Width,
		uint32                    Height,
		FExternalOverlayTargets*& OutTargets);
	static void ReleaseExternalOverlayTargets(FExternalOverlayTargets& Targets);
	void        ReleaseSupplementalTargets();
	void        ReleaseWrappedExternalTargets();

	FGPUTexture2D InternalSceneColorA;
	FGPUTexture2D InternalSceneColorB;
	FGPUTexture2D GameSceneDepth;
	FGPUTexture2D GBufferASurface;
	FGPUTexture2D GBufferBSurface;
	FGPUTexture2D GBufferCSurface;
	FGPUTexture2D OverlayColorSurface;
	FGPUTexture2D OutlineMaskSurface;
	FGPUTexture2D WrappedFinalSceneColor;
	FGPUTexture2D WrappedSceneDepth;

	uint32 GameSceneTargetCacheWidth     = 0;
	uint32 GameSceneTargetCacheHeight    = 0;
	uint32 SupplementalTargetCacheWidth  = 0;
	uint32 SupplementalTargetCacheHeight = 0;

	std::unordered_map<uint64, FExternalOverlayTargets> ExternalOverlayTargetMap;
};
