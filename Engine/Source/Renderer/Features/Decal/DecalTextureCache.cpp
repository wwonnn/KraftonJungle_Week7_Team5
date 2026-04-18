#include "Renderer/Features/Decal/DecalTextureCache.h"

#include "Renderer/Scene/SceneViewData.h"
#include "ThirdParty/stb_image.h"
#include "Debug/EngineLog.h"

#include <fstream>
#include <cmath>
#include <vector>

namespace
{
    static constexpr uint32      DECAL_MAX_TEXTURE_SLICES = 16;
    static constexpr DXGI_FORMAT GDecalBaseColorTextureFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    static constexpr DXGI_FORMAT GDecalBaseColorSRVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    static const std::wstring    GSpotLightFakeCircularMaskPath = L"__SpotLightFakeCircularMask__";

    std::wstring NormalizeDecalTexturePath(const std::wstring& TexturePath)
    {
        if (TexturePath.empty() || TexturePath == GSpotLightFakeCircularMaskPath)
        {
            return TexturePath;
        }

        return std::filesystem::path(TexturePath).lexically_normal().wstring();
    }

    std::vector<unsigned char> CreateCircularMaskPixels(uint32 Width, uint32 Height)
    {
        std::vector<unsigned char> Pixels(Width * Height * 4, 255u);
        const float InvWidth = Width > 1 ? 1.0f / static_cast<float>(Width - 1) : 0.0f;
        const float InvHeight = Height > 1 ? 1.0f / static_cast<float>(Height - 1) : 0.0f;

        for (uint32 Y = 0; Y < Height; ++Y)
        {
            for (uint32 X = 0; X < Width; ++X)
            {
                const float U = static_cast<float>(X) * InvWidth;
                const float V = static_cast<float>(Y) * InvHeight;
                const float DX = U * 2.0f - 1.0f;
                const float DY = V * 2.0f - 1.0f;
                const float Distance = std::sqrt(DX * DX + DY * DY);
                const float Alpha = std::clamp((1.0f - Distance) * 16.0f, 0.0f, 1.0f);

                const size_t PixelIndex = static_cast<size_t>(Y) * Width * 4 + static_cast<size_t>(X) * 4;
                Pixels[PixelIndex + 0] = 255u;
                Pixels[PixelIndex + 1] = 255u;
                Pixels[PixelIndex + 2] = 255u;
                Pixels[PixelIndex + 3] = static_cast<unsigned char>(Alpha * 255.0f);
            }
        }

        return Pixels;
    }
}

bool FDecalTextureCache::CreateSolidColorTextureSRV(ID3D11Device* Device, uint32 PackedRGBA, ID3D11ShaderResourceView** OutSRV)
{
    if (Device == nullptr || OutSRV == nullptr)
    {
        return false;
    }

    *OutSRV = nullptr;

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = 1;
    Desc.Height = 1;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = GDecalBaseColorTextureFormat;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    const D3D11_SUBRESOURCE_DATA InitData = { &PackedRGBA, sizeof(PackedRGBA), 0 };

    ID3D11Texture2D* Texture = nullptr;
    if (FAILED(Device->CreateTexture2D(&Desc, &InitData, &Texture)) || !Texture)
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = GDecalBaseColorSRVFormat;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MipLevels = 1;

    const HRESULT Hr = Device->CreateShaderResourceView(Texture, &SRVDesc, OutSRV);
    Texture->Release();
    return SUCCEEDED(Hr);
}

bool FDecalTextureCache::LoadTexturePixels(const std::wstring& TexturePath, std::vector<unsigned char>& OutPixels, uint32& OutWidth, uint32& OutHeight)
{
    OutPixels.clear();
    OutWidth = 0;
    OutHeight = 0;

    std::ifstream File(TexturePath, std::ios::binary | std::ios::ate);
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

    int W = 0;
    int H = 0;
    int C = 0;
    unsigned char* Data = stbi_load_from_memory(FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
    if (!Data)
    {
        return false;
    }

    OutWidth = static_cast<uint32>(W);
    OutHeight = static_cast<uint32>(H);
    OutPixels.assign(Data, Data + (W * H * 4));
    stbi_image_free(Data);
    return true;
}

bool FDecalTextureCache::InitializeFallbackTexture(ID3D11Device* Device)
{
    if (FallbackBaseColorSRV)
    {
        return true;
    }

    return CreateSolidColorTextureSRV(Device, 0xFFFFFFFFu, &FallbackBaseColorSRV);
}

ID3D11ShaderResourceView* FDecalTextureCache::GetOrLoadBaseColorTexture(ID3D11Device* Device, const std::wstring& TexturePath)
{
    if (TexturePath.empty())
    {
        return FallbackBaseColorSRV;
    }

    const std::wstring NormalizedPath = std::filesystem::path(TexturePath).lexically_normal().wstring();
    auto Found = BaseColorTextureCache.find(NormalizedPath);
    if (Found != BaseColorTextureCache.end())
    {
        return Found->second;
    }

    if (!Device)
    {
        return FallbackBaseColorSRV;
    }

    std::vector<unsigned char> Pixels;
    uint32 Width = 0;
    uint32 Height = 0;
    if (!LoadTexturePixels(NormalizedPath, Pixels, Width, Height) || Pixels.empty())
    {
        return FallbackBaseColorSRV;
    }

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = GDecalBaseColorTextureFormat;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA InitData = { Pixels.data(), Width * 4, 0 };
    ID3D11Texture2D* Texture = nullptr;
    if (FAILED(Device->CreateTexture2D(&Desc, &InitData, &Texture)) || !Texture)
    {
        return FallbackBaseColorSRV;
    }

    ID3D11ShaderResourceView* LoadedSRV = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = GDecalBaseColorSRVFormat;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MipLevels = 1;

    const HRESULT Hr = Device->CreateShaderResourceView(Texture, &SRVDesc, &LoadedSRV);
    Texture->Release();
    if (FAILED(Hr) || !LoadedSRV)
    {
        return FallbackBaseColorSRV;
    }

    BaseColorTextureCache.emplace(NormalizedPath, LoadedSRV);
    return LoadedSRV;
}

void FDecalTextureCache::ResolveTextureArray(ID3D11Device* Device, FSceneViewData& InOutSceneViewData)
{
    auto& DecalItems = InOutSceneViewData.PostProcessInputs.DecalItems;

    TArray<std::wstring> UniquePaths;
    UniquePaths.reserve(DecalItems.size());

    for (const FDecalRenderItem& Item : DecalItems)
    {
        if (Item.TexturePath.empty())
        {
            continue;
        }

        UniquePaths.push_back(NormalizeDecalTexturePath(Item.TexturePath));
    }

    std::sort(UniquePaths.begin(), UniquePaths.end());
    UniquePaths.erase(std::unique(UniquePaths.begin(), UniquePaths.end()), UniquePaths.end());

    TArray<std::wstring> SlicePaths;
    SlicePaths.reserve((std::min)(UniquePaths.size() + 1, static_cast<size_t>(DECAL_MAX_TEXTURE_SLICES)));
    SlicePaths.push_back(L"");

    TMap<std::wstring, uint32> PathToSlice;
    PathToSlice.emplace(L"", 0u);

    for (const std::wstring& Path : UniquePaths)
    {
        if (Path.empty())
        {
            continue;
        }

        if (SlicePaths.size() >= DECAL_MAX_TEXTURE_SLICES)
        {
            UE_LOG("[Decal] Texture array overflow (%u slices). Using fallback for %ls", static_cast<uint32>(DECAL_MAX_TEXTURE_SLICES), Path.c_str());
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(SlicePaths.size());
        SlicePaths.push_back(Path);
        PathToSlice.emplace(Path, NewIndex);
    }

    for (FDecalRenderItem& Item : DecalItems)
    {
        Item.TextureIndex = 0;
        if (Item.TexturePath.empty())
        {
            continue;
        }

        const std::wstring NormalizedPath = NormalizeDecalTexturePath(Item.TexturePath);
        auto Found = PathToSlice.find(NormalizedPath);
        if (Found != PathToSlice.end())
        {
            Item.TextureIndex = Found->second;
        }
    }

    if (SlicePaths == BaseColorTextureArrayPaths && BaseColorTextureArraySRV)
    {
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = BaseColorTextureArraySRV;
        return;
    }

    if (BaseColorTextureArraySRV)
    {
        BaseColorTextureArraySRV->Release();
        BaseColorTextureArraySRV = nullptr;
    }
    if (BaseColorTextureArrayResource)
    {
        BaseColorTextureArrayResource->Release();
        BaseColorTextureArrayResource = nullptr;
    }
    BaseColorTextureArrayPaths = SlicePaths;

    struct FSlicePixels
    {
        std::vector<unsigned char> Pixels;
        uint32 W = 0;
        uint32 H = 0;
    };

    const uint32 ArraySize = static_cast<uint32>(SlicePaths.size());
    std::vector<FSlicePixels> Slices(ArraySize);

    Slices[0].W = 1;
    Slices[0].H = 1;
    Slices[0].Pixels = { 255, 255, 255, 255 };

    uint32 CanonicalW = 0;
    uint32 CanonicalH = 0;

    for (uint32 i = 1; i < ArraySize; ++i)
    {
        const std::wstring& Path = SlicePaths[i];
        if (Path == GSpotLightFakeCircularMaskPath)
        {
            const uint32 Width = CanonicalW > 0 ? CanonicalW : 64u;
            const uint32 Height = CanonicalH > 0 ? CanonicalH : 64u;
            if (CanonicalW == 0)
            {
                CanonicalW = Width;
                CanonicalH = Height;
            }

            Slices[i].W = CanonicalW;
            Slices[i].H = CanonicalH;
            Slices[i].Pixels = CreateCircularMaskPixels(CanonicalW, CanonicalH);
            continue;
        }

        std::vector<unsigned char> Pixels;
        uint32 Width = 0;
        uint32 Height = 0;
        if (!LoadTexturePixels(Path, Pixels, Width, Height) || Pixels.empty())
        {
            UE_LOG("[Decal] Cannot load texture for array: %ls. Using fallback.", Path.c_str());
            continue;
        }

        if (CanonicalW == 0)
        {
            CanonicalW = Width;
            CanonicalH = Height;
        }

        if (Width == CanonicalW && Height == CanonicalH)
        {
            Slices[i].W = Width;
            Slices[i].H = Height;
            Slices[i].Pixels = std::move(Pixels);
        }
        else
        {
            UE_LOG("[Decal] Texture array size mismatch (%ls: %ux%u, expected %ux%u). Using fallback.",
                Path.c_str(), Width, Height, CanonicalW, CanonicalH);
        }
    }

    if (CanonicalW == 0 || CanonicalH == 0)
    {
        CanonicalW = 1;
        CanonicalH = 1;
    }

	// Slices[0] is always the 1x1 white fallback — resize it to match CanonicalW/H
	// so all slices are uniform, as required by a Texture2DArray.
	Slices[0].W = CanonicalW;
	Slices[0].H = CanonicalH;
	Slices[0].Pixels.assign(static_cast<size_t>(CanonicalW)* CanonicalH * 4u, 255u);


    for (uint32 i = 0; i < ArraySize; ++i)
    {
        if (Slices[i].Pixels.empty())
        {
            Slices[i].W = CanonicalW;
            Slices[i].H = CanonicalH;
            Slices[i].Pixels.assign(static_cast<size_t>(CanonicalW) * CanonicalH * 4u, 255u);
        }
		else if (SlicePaths[i] == GSpotLightFakeCircularMaskPath &&
			(Slices[i].W != CanonicalW || Slices[i].H != CanonicalH))
		{
			// Circular mask was generated before CanonicalW/H was known — regenerate
			// it at the correct size now that all textures have been processed.
			Slices[i].W = CanonicalW;
			Slices[i].H = CanonicalH;
			Slices[i].Pixels = CreateCircularMaskPixels(CanonicalW, CanonicalH);
		}
    }

    if (!Device)
    {
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = FallbackBaseColorSRV;
        return;
    }

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = CanonicalW;
    Desc.Height = CanonicalH;
    Desc.MipLevels = 1;
    Desc.ArraySize = ArraySize;
    Desc.Format = GDecalBaseColorTextureFormat;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> InitData(ArraySize);
    for (uint32 i = 0; i < ArraySize; ++i)
    {
        InitData[i].pSysMem = Slices[i].Pixels.data();
        InitData[i].SysMemPitch = CanonicalW * 4;
        InitData[i].SysMemSlicePitch = static_cast<UINT>(Slices[i].Pixels.size());
    }

    if (FAILED(Device->CreateTexture2D(&Desc, InitData.data(), &BaseColorTextureArrayResource)) || !BaseColorTextureArrayResource)
    {
        BaseColorTextureArrayResource = nullptr;
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = FallbackBaseColorSRV;
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = GDecalBaseColorSRVFormat;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.ArraySize = ArraySize;

    if (FAILED(Device->CreateShaderResourceView(BaseColorTextureArrayResource, &SRVDesc, &BaseColorTextureArraySRV)) || !BaseColorTextureArraySRV)
    {
        BaseColorTextureArrayResource->Release();
        BaseColorTextureArrayResource = nullptr;
        BaseColorTextureArraySRV = nullptr;
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = FallbackBaseColorSRV;
        return;
    }

    InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = BaseColorTextureArraySRV;
}

void FDecalTextureCache::Release()
{
    for (auto& Entry : BaseColorTextureCache)
    {
        if (Entry.second)
        {
            Entry.second->Release();
        }
    }
    BaseColorTextureCache.clear();

    if (BaseColorTextureArraySRV)
    {
        BaseColorTextureArraySRV->Release();
        BaseColorTextureArraySRV = nullptr;
    }
    if (BaseColorTextureArrayResource)
    {
        BaseColorTextureArrayResource->Release();
        BaseColorTextureArrayResource = nullptr;
    }
    BaseColorTextureArrayPaths.clear();

    if (FallbackBaseColorSRV)
    {
        FallbackBaseColorSRV->Release();
        FallbackBaseColorSRV = nullptr;
    }
}
