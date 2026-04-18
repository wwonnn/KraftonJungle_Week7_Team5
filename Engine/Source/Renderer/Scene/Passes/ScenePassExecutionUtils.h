#pragma once

#include "Renderer/Scene/Passes/PassContext.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"

#include <algorithm>
#include <cstring>
#include "Renderer/Renderer.h"

namespace DecalPassExecutionUtils
{
	static constexpr uint64 GDecalSignatureOffsetBasis = 1469598103934665603ull;
	static constexpr uint64 GDecalSignaturePrime       = 1099511628211ull;

	inline uint64 HashBytes(uint64 Hash, const void* Data, size_t Size)
	{
		auto Bytes = reinterpret_cast<const unsigned char*>(Data);
		for (size_t Index = 0; Index < Size; ++Index)
		{
			Hash ^= static_cast<uint64>(Bytes[Index]);
			Hash *= GDecalSignaturePrime;
		}
		return Hash;
	}

	template <typename TValue>
	void HashValue(uint64& InOutHash, const TValue& Value)
	{
		InOutHash = HashBytes(InOutHash, &Value, sizeof(TValue));
	}

	inline uint64 BuildPerItemSignatureKey(const FDecalRenderItem& Item, bool bUseVisibleRevision)
	{
		uint64 Hash = GDecalSignatureOffsetBasis;
		HashValue(Hash, Item.SourceComponentId);
		if (bUseVisibleRevision)
		{
			HashValue(Hash, Item.VisibleRevision);
		}
		else
		{
			HashValue(Hash, Item.ClusterRevision);
		}
		return Hash;
	}

	inline uint64 BuildItemRevisionSignature(const FDecalRenderRequest& Request, bool bUseVisibleRevision)
	{
		uint64       Hash      = GDecalSignatureOffsetBasis;
		const uint32 ItemCount = static_cast<uint32>(Request.Items.size());
		HashValue(Hash, ItemCount);

		TArray<uint64> Keys;
		Keys.reserve(Request.Items.size());
		for (const FDecalRenderItem& Item : Request.Items)
		{
			Keys.push_back(BuildPerItemSignatureKey(Item, bUseVisibleRevision));
		}

		if (Request.bSortByPriority)
		{
			std::sort(Keys.begin(), Keys.end());
		}

		for (const uint64 Key : Keys)
		{
			HashValue(Hash, Key);
		}
		return Hash;
	}

	inline uint64 BuildVisibleSignature(const FDecalRenderRequest& Request)
	{
		uint64 Hash = BuildItemRevisionSignature(Request, true);
		HashValue(Hash, Request.bEnabled);
		HashValue(Hash, Request.bSortByPriority);
		HashValue(Hash, Request.ReceiverLayerMask);
		return Hash;
	}

	inline uint64 BuildClusterSignature(const FDecalRenderRequest& Request)
	{
		uint64 Hash = BuildItemRevisionSignature(Request, false);
		HashValue(Hash, Request.VisibleSignature);
		HashValue(Hash, Request.View);
		HashValue(Hash, Request.Projection);
		HashValue(Hash, Request.ViewProjection);
		HashValue(Hash, Request.ViewportWidth);
		HashValue(Hash, Request.ViewportHeight);
		HashValue(Hash, Request.NearZ);
		HashValue(Hash, Request.FarZ);
		HashValue(Hash, Request.ClusterCountX);
		HashValue(Hash, Request.ClusterCountY);
		HashValue(Hash, Request.ClusterCountZ);
		HashValue(Hash, Request.bClampClusterItemCount);
		HashValue(Hash, Request.MaxClusterItems);
		HashValue(Hash, Request.ReceiverLayerMask);
		return Hash;
	}
}

inline FDecalRenderRequest BuildDecalPassRequest(const FSceneViewData& SceneViewData, EDecalDirtyFlags DirtyFlags)
{
	FDecalRenderRequest Request;
	Request.Items                        = SceneViewData.PostProcessInputs.DecalItems;
	Request.View                         = SceneViewData.View.View;
	Request.Projection                   = SceneViewData.View.Projection;
	Request.ViewProjection               = SceneViewData.View.ViewProjection;
	Request.InverseViewProjection        = SceneViewData.View.InverseViewProjection;
	Request.CameraPosition               = SceneViewData.View.CameraPosition;
	Request.ViewportWidth                = static_cast<uint32>(SceneViewData.View.Viewport.Width);
	Request.ViewportHeight               = static_cast<uint32>(SceneViewData.View.Viewport.Height);
	Request.NearZ                        = SceneViewData.View.NearZ;
	Request.FarZ                         = SceneViewData.View.FarZ;
	Request.ClusterCountX                = 16;
	Request.ClusterCountY                = 9;
	Request.ClusterCountZ                = 24;
	Request.ReceiverLayerMask            = 0xFFFFFFFFu;
	Request.BaseColorTextureArraySRV     = SceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV;
	Request.CandidateReceiverObjectCount = static_cast<uint32>(SceneViewData.MeshInputs.Batches.size());
	Request.bDebugDraw                   = SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugVolume)
			&& SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DecalDebug)
			&& !SceneViewData.bForceWireframe;
	Request.DirtyFlags       = DirtyFlags;
	Request.VisibleSignature = DecalPassExecutionUtils::BuildVisibleSignature(Request);
	Request.ClusterSignature = DecalPassExecutionUtils::BuildClusterSignature(Request);
	return Request;
}

inline FDecalRenderRequest BuildLocalFogDebugPassRequest(const FSceneViewData& SceneViewData)
{
	FDecalRenderRequest Request;
	for (const FFogRenderItem& FogItem : SceneViewData.PostProcessInputs.FogItems)
	{
		if (!FogItem.IsLocalFogVolume())
		{
			continue;
		}

		FDecalRenderItem& Item = Request.Items.emplace_back();
		Item.DecalWorld        = FogItem.FogVolumeWorld;
		Item.WorldToDecal      = FogItem.WorldToFogVolume;
		Item.Extents           = FVector(0.5f, 0.5f, 0.5f);
		Item.Flags             = DECAL_RENDER_FLAG_BaseColor;
		Item.Priority          = 0u;
		Item.ReceiverLayerMask = 0xFFFFFFFFu;
		Item.AtlasScaleBias    = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
		Item.BaseColorTint     = FogItem.FogInscatteringColor;
		Item.EdgeFade          = 1.0f;
		Item.AllowAngle        = 0.0f;
	}

	Request.View                         = SceneViewData.View.View;
	Request.Projection                   = SceneViewData.View.Projection;
	Request.ViewProjection               = SceneViewData.View.ViewProjection;
	Request.InverseViewProjection        = SceneViewData.View.InverseViewProjection;
	Request.CameraPosition               = SceneViewData.View.CameraPosition;
	Request.ViewportWidth                = static_cast<uint32>(SceneViewData.View.Viewport.Width);
	Request.ViewportHeight               = static_cast<uint32>(SceneViewData.View.Viewport.Height);
	Request.NearZ                        = SceneViewData.View.NearZ;
	Request.FarZ                         = SceneViewData.View.FarZ;
	Request.ClusterCountX                = 16;
	Request.ClusterCountY                = 9;
	Request.ClusterCountZ                = 24;
	Request.ReceiverLayerMask            = 0xFFFFFFFFu;
	Request.CandidateReceiverObjectCount = static_cast<uint32>(SceneViewData.MeshInputs.Batches.size());
	Request.bDebugDraw                   = SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugVolume)
			&& SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_LocalFogDebug)
			&& !SceneViewData.bForceWireframe;
	return Request;
}

inline FOutlineRenderRequest BuildOutlinePassRequest(const FSceneViewData& SceneViewData)
{
	FOutlineRenderRequest Request;
	Request.bEnabled = SceneViewData.PostProcessInputs.bOutlineEnabled;
	Request.Items    = SceneViewData.PostProcessInputs.OutlineItems;
	return Request;
}

inline bool ExecuteMeshScenePass(
	FRenderer&                Renderer,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData,
	const FMeshPassProcessor& Processor,
	EMeshPassType             PassType)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
	Processor.ExecutePass(Renderer, Targets, SceneViewData, PassType);
	EndPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
	return true;
}
