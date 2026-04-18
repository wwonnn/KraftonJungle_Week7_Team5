#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Fog/FogStats.h"
#include "Renderer/GPUStats.h"
#include "Renderer/Features/Outline/OutlineTypes.h"
#include "Renderer/Features/Decal/DecalProjectionMode.h"
#include "Renderer/Features/Decal/DecalStats.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/GraphicsCore/RenderDevice.h"
#include "Renderer/Common/RenderFeatureInterfaces.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Renderer/Common/RenderMode.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/GraphicsCore/RenderStateManager.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/UI/Screen/UIDrawList.h"
#include "Renderer/Resources/Shader/ShaderManager.h"

#include <d3d11.h>
#include <filesystem>
#include <memory>

struct FVertex;
struct FRenderMesh;
struct FMeshPassFrameStats;
enum class EPassDomain : uint8;
class FPixelShader;
class FMaterial;
class ULevel;
class UWorld;
class AActor;
class FSceneRenderer;
class FViewportCompositor;
class FScreenUIRenderer;
class FSceneTargetManager;
class FDecalTextureCache;
class FTextRenderFeature;
class FSubUVRenderFeature;
class FBillboardRenderFeature;
class FFogRenderFeature;
class FOutlineRenderFeature;
class FDecalRenderFeature;
class FVolumeDecalRenderFeature;
class FFireBallRenderFeature;
class FFXAARenderFeature;
class FDebugLineRenderFeature;
class FBillboardRenderer;
class FDebugDrawManager;
class FToneMappingRenderFeature;
struct FScreenUIPassInputs;

class ENGINE_API FRenderer
{
public:
	FRenderer(HWND InHwnd, int32 InWidth, int32 InHeight);
	~FRenderer();

	bool Initialize(HWND InHwnd, int32 InWidth, int32 InHeight);
	void BeginFrame();
	void EndFrame();
	void Release();
	bool IsOccluded();
	void OnResize(int32 NewWidth, int32 NewHeight);

	void SetVSync(bool bEnable)
	{
		RenderDevice.SetVSync(bEnable);
	}

	bool IsVSyncEnabled() const
	{
		return RenderDevice.IsVSyncEnabled();
	}

	bool RenderScreenUIPass(
		const FScreenUIPassInputs& PassInputs,
		const FFrameContext&       Frame,
		ID3D11RenderTargetView*    RenderTargetView,
		ID3D11DepthStencilView*    DepthStencilView = nullptr);
	bool ComposeViewports(
		const FViewportCompositePassInputs& Inputs,
		const FFrameContext&                Frame,
		const FViewContext&                 View,
		ID3D11RenderTargetView*             RenderTargetView,
		ID3D11DepthStencilView*             DepthStencilView = nullptr);
	bool RenderGameFrame(const FGameFrameRequest& Request);
	bool RenderEditorFrame(const FEditorFrameRequest& Request);

	bool CreateTextureFromSTB(ID3D11Device* Device, const char* FilePath, ID3D11ShaderResourceView** OutSRV);
	bool CreateTextureFromSTB(ID3D11Device* Device, const std::filesystem::path& FilePath, ID3D11ShaderResourceView** OutSRV);

	void ConfigureMaterialPasses(FMaterial& Material, bool bTexturedMaterial);

	FMaterial* GetDefaultMaterial() const
	{
		return DefaultMaterial.get();
	}

	FMaterial* GetDefaultTextureMaterial() const
	{
		return DefaultTextureMaterial.get();
	}

	size_t GetPrevCommandCount() const;

	std::unique_ptr<FRenderStateManager>& GetRenderStateManager()
	{
		return RenderStateManager;
	}

	ID3D11Device* GetDevice() const
	{
		return RenderDevice.GetDevice();
	}

	ID3D11DeviceContext* GetDeviceContext() const
	{
		return RenderDevice.GetDeviceContext();
	}

	ID3D11RenderTargetView* GetRenderTargetView() const
	{
		return RenderDevice.GetRenderTargetView();
	}

	IDXGISwapChain* GetSwapChain() const
	{
		return RenderDevice.GetSwapChain();
	}

	HWND GetHwnd() const
	{
		return RenderDevice.GetHwnd();
	}

	const D3D11_VIEWPORT& GetBackBufferViewport() const
	{
		return RenderDevice.GetViewport();
	}

	ISceneTextFeature*         GetSceneTextFeature() const;
	ISceneSubUVFeature*        GetSceneSubUVFeature() const;
	ISceneBillboardFeature*    GetSceneBillboardFeature() const;
	FFogRenderFeature*         GetFogFeature() const;
	FOutlineRenderFeature*     GetOutlineFeature() const;
	FDebugLineRenderFeature*   GetDebugLineFeature() const;
	FDecalRenderFeature*       GetDecalFeature() const;
	FVolumeDecalRenderFeature* GetVolumeDecalFeature() const;
	FFireBallRenderFeature*    GetFireBallFeature() const;
	FFXAARenderFeature*        GetFXAAFeature() const;
	FToneMappingRenderFeature* GetToneMappingRenderFeature() const;

	FSceneRenderer& GetSceneRenderer()
	{
		return *SceneRenderer;
	}

	FScreenUIRenderer& GetScreenUIRenderer()
	{
		return *ScreenUIRenderer;
	}

	FRenderDevice& GetRenderDevice()
	{
		return RenderDevice;
	}

	FBillboardRenderer&     GetBillboardRenderer();
	const FDecalFrameStats& GetDecalFrameStats() const;
	FMeshPassFrameStats     GetMeshPassFrameStats() const;

	void SetDecalProjectionMode(EDecalProjectionMode InMode)
	{
		DecalProjectionMode = InMode;
	}

	EDecalProjectionMode GetDecalProjectionMode() const
	{
		return DecalProjectionMode;
	}

	FDecalStats    GetDecalStats() const;
	FFogStats      GetFogStats() const;
	FGPUFrameStats GetGPUStats() const;

	ID3D11SamplerState* GetDefaultSampler() const
	{
		return NormalSampler;
	}

	void SetConstantBuffers();
	void UpdateFrameConstantBuffer(const FFrameContext& Frame, const FViewContext& View);
	void UpdateObjectConstantBuffer(const FMatrix& WorldMatrix);
	void ClearDepthBuffer(ID3D11DepthStencilView* DepthStencilView);
	void PreparePassDomain(EPassDomain Domain, const FSceneRenderTargets& Targets);
	bool ResolveSceneColorTargets(const FSceneRenderTargets& Targets);

	ID3D11ShaderResourceView* GetFolderIconSRV() const
	{
		return FolderIconSRV;
	}

	ID3D11ShaderResourceView* GetFileIconSRV() const
	{
		return FileIconSRV;
	}

	FShaderManager ShaderManager;

private:
	friend class FSceneRenderer;
	friend class FTextRenderFeature;
	friend class FBillboardRenderFeature;
	friend class FFogRenderFeature;
	friend class FOutlineRenderFeature;
	friend class FDebugLineRenderFeature;
	friend class FDecalRenderFeature;
	friend class FVolumeDecalRenderFeature;
	friend class FScreenUIRenderer;
	friend class FRendererResourceBootstrap;
	friend class FGameFrameRenderer;
	friend class FEditorFrameRenderer;
	bool CreateConstantBuffers();
	bool CreateSamplers();

	bool CreateSceneColorResolveResources();
	void ReleaseSceneColorResolveResources();

	std::unique_ptr<FRenderStateManager> RenderStateManager = nullptr;

	FRenderDevice RenderDevice;

	ID3D11Buffer* FrameConstantBuffer  = nullptr;
	ID3D11Buffer* ObjectConstantBuffer = nullptr;

	std::shared_ptr<FMaterial> DefaultMaterial;
	std::shared_ptr<FMaterial> DefaultTextureMaterial;

	std::unique_ptr<FSceneRenderer>            SceneRenderer;
	std::unique_ptr<FViewportCompositor>       ViewportCompositor;
	std::unique_ptr<FScreenUIRenderer>         ScreenUIRenderer;
	std::unique_ptr<FTextRenderFeature>        TextFeature;
	std::unique_ptr<FSubUVRenderFeature>       SubUVFeature;
	std::unique_ptr<FBillboardRenderFeature>   BillboardFeature;
	std::unique_ptr<FFogRenderFeature>         FogFeature;
	std::unique_ptr<FOutlineRenderFeature>     OutlineFeature;
	std::unique_ptr<FDebugLineRenderFeature>   DebugLineFeature;
	std::unique_ptr<FDecalRenderFeature>       DecalFeature;
	std::unique_ptr<FVolumeDecalRenderFeature> VolumeDecalFeature;
	std::unique_ptr<FFireBallRenderFeature>    FireBallFeature;
	std::unique_ptr<FFXAARenderFeature>        FXAAFeature;
	std::unique_ptr<FToneMappingRenderFeature> FToneMappingFeature;
	EDecalProjectionMode                       DecalProjectionMode = EDecalProjectionMode::ClusteredLookup;

	ID3D11ShaderResourceView*            FolderIconSRV = nullptr;
	ID3D11ShaderResourceView*            FileIconSRV   = nullptr;
	std::unique_ptr<FSceneTargetManager> SceneTargetManager;
	std::unique_ptr<FDecalTextureCache>  DecalTextureCache;
	ID3D11SamplerState*                  NormalSampler = nullptr;

	ID3D11VertexShader* SceneColorResolveVertexShader = nullptr;
	ID3D11PixelShader*  SceneColorResolvePixelShader  = nullptr;
	ID3D11SamplerState* SceneColorResolveSampler      = nullptr;
};
