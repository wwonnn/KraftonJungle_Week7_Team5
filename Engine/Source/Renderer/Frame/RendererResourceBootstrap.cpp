#include "Renderer/Frame/RendererResourceBootstrap.h"

#include "Renderer/Renderer.h"

#include "Core/Paths.h"
#include "Renderer/Features/Billboard/BillboardRenderFeature.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Features/SubUV/SubUVRenderFeature.h"
#include "Renderer/Features/Text/TextRenderFeature.h"
#include "Renderer/Features/PostProcess/ToneMappingRenderFeature.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/ShaderType.h"

bool FRendererResourceBootstrap::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	std::wstring ShaderDirW = FPaths::ShaderDir();
	std::wstring VSPath     = ShaderDirW + L"SceneGeometry/VertexShader.hlsl";
	std::wstring PSPath     = ShaderDirW + L"SceneGeometry/PixelShader.hlsl";

	if (!Renderer.ShaderManager.LoadVertexShader(Device, VSPath.c_str()))
	{
		return false;
	}
	if (!Renderer.ShaderManager.LoadPixelShader(Device, PSPath.c_str()))
	{
		return false;
	}

	{
		auto         VS          = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str());
		std::wstring ColorPSPath = ShaderDirW + L"SceneGeometry/ColorPixelShader.hlsl";
		auto         PS          = FShaderMap::Get().GetOrCreatePixelShader(Device, ColorPSPath.c_str());
		Renderer.DefaultMaterial = std::make_shared<FMaterial>();
		Renderer.DefaultMaterial->SetOriginName("M_Default");
		Renderer.DefaultMaterial->SetVertexShader(VS);
		Renderer.DefaultMaterial->SetPixelShader(PS);

		FRasterizerStateOption rasterizerOption;
		rasterizerOption.FillMode = D3D11_FILL_SOLID;
		rasterizerOption.CullMode = D3D11_CULL_BACK;
		auto RS                   = Renderer.RenderStateManager->GetOrCreateRasterizerState(rasterizerOption);
		Renderer.DefaultMaterial->SetRasterizerOption(rasterizerOption);
		Renderer.DefaultMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption depthStencilOption;
		depthStencilOption.DepthEnable    = true;
		depthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		auto DSS                          = Renderer.RenderStateManager->GetOrCreateDepthStencilState(depthStencilOption);
		Renderer.DefaultMaterial->SetDepthStencilOption(depthStencilOption);
		Renderer.DefaultMaterial->SetDepthStencilState(DSS);

		int32 SlotIndex = Renderer.DefaultMaterial->CreateConstantBuffer(Device, 16);
		if (SlotIndex >= 0)
		{
			Renderer.DefaultMaterial->RegisterParameter("BaseColor", SlotIndex, 0, 16);
			float White[4] = {1.0f, 1.0f, 1.0f, 1.0f};
			Renderer.DefaultMaterial->GetConstantBuffer(SlotIndex)->SetData(White, sizeof(White));
		}

		Renderer.ConfigureMaterialPasses(*Renderer.DefaultMaterial, false);
		FMaterialManager::Get().Register("M_Default", Renderer.DefaultMaterial);
	}

	{
		std::wstring TextureVSPath      = ShaderDirW + L"SceneGeometry/TextureVertexShader.hlsl";
		auto         VS                 = FShaderMap::Get().GetOrCreateVertexShader(Device, TextureVSPath.c_str());
		std::wstring TexturePSPath      = ShaderDirW + L"SceneGeometry/TexturePixelShader.hlsl";
		auto         PS                 = FShaderMap::Get().GetOrCreatePixelShader(Device, TexturePSPath.c_str());
		Renderer.DefaultTextureMaterial = std::make_shared<FMaterial>();
		Renderer.DefaultTextureMaterial->SetOriginName("M_Default_Texture");
		Renderer.DefaultTextureMaterial->SetVertexShader(VS);
		Renderer.DefaultTextureMaterial->SetPixelShader(PS);

		FRasterizerStateOption rasterizerOption;
		rasterizerOption.FillMode = D3D11_FILL_SOLID;
		rasterizerOption.CullMode = D3D11_CULL_BACK;
		auto RS                   = Renderer.RenderStateManager->GetOrCreateRasterizerState(rasterizerOption);
		Renderer.DefaultTextureMaterial->SetRasterizerOption(rasterizerOption);
		Renderer.DefaultTextureMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption depthStencilOption;
		depthStencilOption.DepthEnable    = true;
		depthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		auto DSS                          = Renderer.RenderStateManager->GetOrCreateDepthStencilState(depthStencilOption);
		Renderer.DefaultTextureMaterial->SetDepthStencilOption(depthStencilOption);
		Renderer.DefaultTextureMaterial->SetDepthStencilState(DSS);

		int32 SlotIndex = Renderer.DefaultTextureMaterial->CreateConstantBuffer(Device, 32);
		if (SlotIndex >= 0)
		{
			Renderer.DefaultTextureMaterial->RegisterParameter("BaseColor", SlotIndex, 0, 16);
			float White[4] = {1.0f, 1.0f, 1.0f, 1.0f};
			Renderer.DefaultTextureMaterial->GetConstantBuffer(SlotIndex)->SetData(White, sizeof(White));

			Renderer.DefaultTextureMaterial->RegisterParameter("UVScrollSpeed", SlotIndex, 16, 16);
			float DefaultScroll[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			Renderer.DefaultTextureMaterial->GetConstantBuffer(SlotIndex)->SetData(DefaultScroll, sizeof(DefaultScroll), 16);
		}

		Renderer.ConfigureMaterialPasses(*Renderer.DefaultTextureMaterial, true);
		FMaterialManager::Get().Register("M_Default_Texture", Renderer.DefaultTextureMaterial);
	}

	Renderer.TextFeature = std::make_unique<FTextRenderFeature>();
	if (!Renderer.TextFeature || !Renderer.TextFeature->Initialize(Renderer))
	{
		return false;
	}

	std::filesystem::path SubUVTexturePath = FPaths::ContentDir() / FString("Textures/SubUVDino.png");
	Renderer.SubUVFeature                  = std::make_unique<FSubUVRenderFeature>();
	if (!Renderer.SubUVFeature || !Renderer.SubUVFeature->Initialize(Renderer, SubUVTexturePath.wstring()))
	{
		MessageBox(nullptr, L"SubUVRenderer Initialize Failed.", nullptr, 0);
		return false;
	}

	Renderer.BillboardFeature = std::make_unique<FBillboardRenderFeature>();
	if (!Renderer.BillboardFeature || !Renderer.BillboardFeature->Initialize(Renderer))
	{
		return false;
	}

	Renderer.DecalFeature = std::make_unique<FDecalRenderFeature>();
	if (!Renderer.DecalFeature)
	{
		return false;
	}

	Renderer.VolumeDecalFeature = std::make_unique<FVolumeDecalRenderFeature>();
	if (!Renderer.VolumeDecalFeature)
	{
		return false;
	}

	Renderer.FogFeature       = std::make_unique<FFogRenderFeature>();
	Renderer.OutlineFeature   = std::make_unique<FOutlineRenderFeature>();
	Renderer.DebugLineFeature = std::make_unique<FDebugLineRenderFeature>();
	Renderer.FireBallFeature  = std::make_unique<FFireBallRenderFeature>();
	if (!Renderer.FireBallFeature)
	{
		return false;
	}

	Renderer.FToneMappingFeature = std::make_unique<FToneMappingRenderFeature>();
	if (!Renderer.FToneMappingFeature)
	{
		return false;
	}

	Renderer.FXAAFeature = std::make_unique<FFXAARenderFeature>();
	if (!Renderer.FXAAFeature)
	{
		return false;
	}

	std::filesystem::path FolderIconPath = FPaths::AssetDir() / FString("Textures/FolderIcon.png");
	std::filesystem::path FileIconPath   = FPaths::AssetDir() / FString("Textures/FileIcon.png");
	Renderer.CreateTextureFromSTB(Device, FolderIconPath, &Renderer.FolderIconSRV);
	Renderer.CreateTextureFromSTB(Device, FileIconPath, &Renderer.FileIconSRV);
	if (!Renderer.DecalTextureCache->InitializeFallbackTexture(Device))
	{
		return false;
	}

	return true;
}

void FRendererResourceBootstrap::Release(FRenderer& Renderer)
{
	if (Renderer.FogFeature)
	{
		Renderer.FogFeature->Release();
	}
	if (Renderer.OutlineFeature)
	{
		Renderer.OutlineFeature->Release();
	}
	if (Renderer.DebugLineFeature)
	{
		Renderer.DebugLineFeature->Release();
	}
	if (Renderer.TextFeature)
	{
		Renderer.TextFeature->Release();
	}
	if (Renderer.SubUVFeature)
	{
		Renderer.SubUVFeature->Release();
	}
	if (Renderer.BillboardFeature)
	{
		Renderer.BillboardFeature->Release();
	}
	if (Renderer.DecalFeature)
	{
		Renderer.DecalFeature->Release();
	}
	if (Renderer.VolumeDecalFeature)
	{
		Renderer.VolumeDecalFeature->Release();
	}
	if (Renderer.FireBallFeature)
	{
		Renderer.FireBallFeature->Release();
	}
	if (Renderer.FToneMappingFeature)
	{
		Renderer.FToneMappingFeature->Release();
	}
	if (Renderer.FXAAFeature)
	{
		Renderer.FXAAFeature->Release();
	}
	Renderer.OutlineFeature.reset();
	Renderer.DebugLineFeature.reset();
	Renderer.FogFeature.reset();
	Renderer.TextFeature.reset();
	Renderer.SubUVFeature.reset();
	Renderer.BillboardFeature.reset();
	Renderer.DecalFeature.reset();
	Renderer.VolumeDecalFeature.reset();
	Renderer.FireBallFeature.reset();
	Renderer.FToneMappingFeature.reset();
	Renderer.FXAAFeature.reset();
	Renderer.ShaderManager.Release();
	FShaderMap::Get().Clear();
	FMaterialManager::Get().Clear();
	if (Renderer.NormalSampler)
	{
		Renderer.NormalSampler->Release();
		Renderer.NormalSampler = nullptr;
	}
	Renderer.DefaultMaterial.reset();
	Renderer.DefaultTextureMaterial.reset();
	Renderer.DecalTextureCache->Release();
	if (Renderer.FolderIconSRV)
	{
		Renderer.FolderIconSRV->Release();
		Renderer.FolderIconSRV = nullptr;
	}
	if (Renderer.FileIconSRV)
	{
		Renderer.FileIconSRV->Release();
		Renderer.FileIconSRV = nullptr;
	}
	if (Renderer.FrameConstantBuffer)
	{
		Renderer.FrameConstantBuffer->Release();
		Renderer.FrameConstantBuffer = nullptr;
	}
	if (Renderer.ObjectConstantBuffer)
	{
		Renderer.ObjectConstantBuffer->Release();
		Renderer.ObjectConstantBuffer = nullptr;
	}
}
