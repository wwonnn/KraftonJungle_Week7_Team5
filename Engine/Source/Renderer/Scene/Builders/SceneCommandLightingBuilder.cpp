#include "SceneCommandLightingBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneViewData.h"

#include "Actor/Actor.h"
#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Math/MathUtility.h"
#include "World/World.h"

#include "Renderer/Features/Shadow/ShadowAtlasAllocator.h"

#include <algorithm>
#include <cmath>

#include "Math/Cascade.h"

namespace
{
	FVector ToLightRGB(const FLinearColor& Color)
	{
		return FVector(Color.R, Color.G, Color.B);
	}

	FLocalLightRenderItem BuildPointLight(const UPointLightComponent* C)
	{
		FLocalLightRenderItem L;
		L.LightClass = ELightClass::Point;
		L.CullShape  = ECullShapeType::Sphere;

		L.PositionWS      = C->GetWorldLocation();
		L.Range           = C->GetAttenuationRadius();
		L.Color           = ToLightRGB(C->GetColor());
		L.Intensity       = C->GetEffectiveIntensity();
		L.FalloffExponent = C->GetLightFalloffExponent();
		L.Flags           = 0;

		L.CullCenterWS = L.PositionWS;
		L.CullRadius   = L.Range;

		return L;
	}

	FLocalLightRenderItem BuildSpotLight(const USpotLightComponent* C)
	{
		FLocalLightRenderItem L;
		L.LightClass = ELightClass::Spot;
		L.CullShape  = ECullShapeType::Cone;

		L.PositionWS      = C->GetWorldLocation();
		L.DirectionWS     = C->GetEmissionDirectionWS().GetSafeNormal();
		L.Range           = C->GetAttenuationRadius();
		L.FalloffExponent = C->GetLightFalloffExponent();
		L.Color           = ToLightRGB(C->GetColor());
		L.Intensity       = C->GetEffectiveIntensity();
		L.Flags           = 0;

		const float InnerAngleRad = FMath::DegreesToRadians(FMath::Clamp(C->GetInnerConeAngle(), 0.0f, 89.0f));
		const float OuterAngleRad = FMath::DegreesToRadians(FMath::Clamp(C->GetOuterConeAngle(), 0.0f, 89.0f));

		L.InnerAngleCos = std::cos(InnerAngleRad);
		L.OuterAngleCos = std::cos(OuterAngleRad);

		if (L.InnerAngleCos < L.OuterAngleCos)
		{
			std::swap(L.InnerAngleCos, L.OuterAngleCos);
		}

		// conservative sphere
		L.CullCenterWS = L.PositionWS + L.DirectionWS * (L.Range * 0.5f);
		L.CullRadius   = L.Range;

		return L;
	}

	FDirectionalLightRenderItem BuildDirectionalLight(const UDirectionalLightComponent* C)
	{
		FDirectionalLightRenderItem L;
		L.DirectionWS = C->GetEmissionDirectionWS().GetSafeNormal();
		L.Color       = ToLightRGB(C->GetColor());
		L.Intensity   = C->GetEffectiveIntensity();
		L.Flags       = 0;
		return L;
	}

	uint32 AllocalteShadowLight(FSceneLightingInputs& Inputs, EShadowLightType LightType, uint32 SourceLightIndex)
	{
		if (Inputs.ShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType              = LightType;
		Item.SourceLightIndex       = SourceLightIndex;
		Item.ShadowIndex            = static_cast<uint32>(Inputs.ShadowLights.size());
		Inputs.ShadowLights.push_back(Item);

		return Item.ShadowIndex;
	}

	uint32 AddShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, const FShadowViewRenderItem& InView)
	{
		if (Inputs.ShadowViews.size() >= ShadowConfig::MaxShadowViews)
			return UINT32_MAX;

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = static_cast<uint32>(Inputs.ShadowViews.size());

		const uint32 ViewIndex = static_cast<uint32>(Inputs.ShadowViews.size());
		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}

		++ShadowLight.ViewCount;

		return ViewIndex;
	}
	float ComputeSpotScreenCoverage(
		const FVector& LightPosition,
		const FVector& LightDirection,
		float Range,
		float OuterConeAngleDeg,
		const FMatrix& ViewProj)
	{
		const float SafeRange = (std::max)(Range, 0.0f);
		const FVector Direction = LightDirection.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return 0.0f;
		}

		const float OuterAngleRad = FMath::DegreesToRadians(
			FMath::Clamp(OuterConeAngleDeg, 1.0f, 80.0f));

		const float ConeEndRadius = std::tan(OuterAngleRad) * SafeRange;
		const FVector EndCenter = LightPosition + Direction * SafeRange;

		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		if (std::abs(FVector::DotProduct(Direction, Up)) > 0.95f)
		{
			Up = FVector(0.0f, 1.0f, 0.0f);
		}

		const FVector Right = FVector::CrossProduct(Up, Direction).GetSafeNormal();
		const FVector ConeUp = FVector::CrossProduct(Direction, Right).GetSafeNormal();

		float MinX = FLT_MAX;
		float MinY = FLT_MAX;
		float MaxX = -FLT_MAX;
		float MaxY = -FLT_MAX;

		int32 ProjectedCount = 0;

		auto AddProjectedPoint = [&](const FVector& WorldPos)
			{
				const FVector4 Clip = ViewProj.TransformVector4(FVector4(WorldPos, 1.0f));

				// Near plane 뒤에 있는 점은 일단 제외.
				// 완전 정확하게 하려면 near clipping이 필요하지만, shadow resolution 용도면 이 정도로 충분.
				if (Clip.W <= 1.0e-4f)
					return;

				const float InvW = 1.0f / Clip.W;
				const float NdcX = Clip.X * InvW;
				const float NdcY = Clip.Y * InvW;

				MinX = std::min(MinX, NdcX);
				MinY = std::min(MinY, NdcY);
				MaxX = std::max(MaxX, NdcX);
				MaxY = std::max(MaxY, NdcY);

				++ProjectedCount;
			};

		AddProjectedPoint(LightPosition);
		AddProjectedPoint(EndCenter);

		constexpr int32 SegmentCount = 12;

		for (int32 i = 0; i < SegmentCount; ++i)
		{
			const float T = (2.0f * FMath::PI * i) / SegmentCount;

			const FVector P =
				EndCenter
				+ Right * std::cos(T) * ConeEndRadius
				+ ConeUp * std::sin(T) * ConeEndRadius;

			AddProjectedPoint(P);
		}

		if (ProjectedCount == 0)
			return 0.0f;

		MinX = FMath::Clamp(MinX, -1.0f, 1.0f);
		MinY = FMath::Clamp(MinY, -1.0f, 1.0f);
		MaxX = FMath::Clamp(MaxX, -1.0f, 1.0f);
		MaxY = FMath::Clamp(MaxY, -1.0f, 1.0f);

		const float Width = std::max(0.0f, MaxX - MinX);
		const float Height = std::max(0.0f, MaxY - MinY);

		const float Coverage = (Width * Height) * 0.25f;

		return FMath::Clamp(Coverage, 0.0f, 1.0f);
	}

	uint32 QuantizeShadowResolution(uint32 Resolution)
	{
		if (Resolution >= 2048) return 2048;
		if (Resolution >= 1024) return 1024;
		if (Resolution >= 512)  return 512;
		if (Resolution >= 256)  return 256;
		if (Resolution >= 128)  return 128;
		return 64;
	}
	uint32 QuantizeDiraShadowResolution(uint32 Resolution)
	{
		if (Resolution >= 4096) return 4096;
		return QuantizeShadowResolution(Resolution);
	}

	uint32 AddPointShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, uint32 ExplicitArraySlice, const FShadowViewRenderItem& InView) 
	{
		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = ExplicitArraySlice;

		uint32 ViewIndex = static_cast<uint32>(Inputs.ShadowViews.size());
		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& Light = Inputs.ShadowLights[ShadowLightIndex];
		if (Light.ViewCount == 0)
			Light.FirstViewIndex = ViewIndex;
		Light.ViewCount++;
		Light.LightType = EShadowLightType::Point;

		return ViewIndex;
	}

	uint32 AllocateDirShadowLight(FSceneLightingInputs& Inputs, EShadowLightType LightType, uint32 SourceLightIndex)
	{
		if (Inputs.DirShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType = LightType;
		Item.SourceLightIndex = SourceLightIndex;
		Item.ShadowIndex = static_cast<uint32>(Inputs.DirShadowLights.size());
		Inputs.DirShadowLights.push_back(Item);

		return Item.ShadowIndex;
	}

	uint32 AddDirShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, const FShadowViewRenderItem& InView)
	{
		if (Inputs.DirShadowViews.size() >= ShadowConfig::MaxDirCascade)
		{
			return UINT32_MAX;
		}

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = static_cast<uint32>(Inputs.DirShadowViews.size());

		const uint32 ViewIndex = static_cast<uint32>(Inputs.DirShadowViews.size());
		Inputs.DirShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight = Inputs.DirShadowLights[ShadowLightIndex];
		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}
		++ShadowLight.ViewCount;

		return ViewIndex;
	}


	void BuildSpotShadowViews(
		FSceneLightingInputs&        Inputs,
		const USpotLightComponent*   Spot,
		const FLocalLightRenderItem& LightItem,
		uint32                       LocalLightIndex,
		uint32                       ShadowLightIndex,
		const FMatrix&				 ViewProjMatrix)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.LightType   = EShadowLightType::Spot;
		ShadowLight.PositionWS  = LightItem.PositionWS;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Mobility    = Spot->GetMobility();
		ShadowLight.bCacheDirty = Spot->IsCacheDirty();
		Spot->ResetShadowCacheDirty();
		ShadowLight.Bias        = Spot->GetShadowBias();
		ShadowLight.SlopeBias   = Spot->GetShadowSlopeBias();
		ShadowLight.NormalBias  = 0.0f;
		ShadowLight.Sharpen     = Spot->GetShadowSharpen();

		const FVector DirectionWS = LightItem.DirectionWS.GetSafeNormal();

		FVector UpWS = FVector::UpVector;
		if (std::abs(FVector::DotProduct(DirectionWS, UpWS)) > 0.98f)
		{
			UpWS = FVector::RightVector;
		}

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ  = (std::max)(LightItem.Range, NearZ + 0.001f);

		const float OuterHalfAngleDeg = FMath::Clamp(Spot->GetOuterConeAngle(), 1.0f, 80.0f);
		const float FullFovRad        = FMath::DegreesToRadians(OuterHalfAngleDeg * 2.0f);

		const float Coverage = ComputeSpotScreenCoverage(LightItem.PositionWS, LightItem.DirectionWS,LightItem.Range, Spot->GetOuterConeAngle(), ViewProjMatrix);
		if(Coverage <= 0.0f)
		{
			return;
		}

		float ResolutionFactor = std::sqrt(Coverage) * Spot->GetShadowResolutionScale();
		uint32 RequestedResolution = QuantizeShadowResolution(static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionFactor));

		FShadowViewRenderItem View;
		View.ProjectionType      = EShadowProjectionType::Perspective;
		View.PositionWS          = LightItem.PositionWS;
		View.NearZ               = NearZ;
		View.FarZ                = FarZ;
		View.SourceActor         = Spot->GetOwner();

		View.View = FMatrix::MakeViewLookAtLH(
			LightItem.PositionWS,
			LightItem.PositionWS + DirectionWS,
			UpWS);

		View.Projection = FMatrix::MakePerspectiveFovLH(
			FullFovRad,
			1.0f,
			NearZ,
			FarZ);

		View.ViewProjection = View.View * View.Projection;
		View.LightType = EShadowLightType::Spot;

		View.Viewport = {};
		View.RequestedResolution = QuantizeShadowResolution(RequestedResolution);

		AddShadowView(Inputs, ShadowLightIndex, View);
	}

	void BuildDirectionalShadowViews(
		FSceneLightingInputs& Inputs,
		const UDirectionalLightComponent* DirLight,
		FDirectionalLightRenderItem& LightItem,
		uint32 ShadowLightIndex,
		const FViewContext& View)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.DirShadowLights[ShadowLightIndex];
		ShadowLight.LightType = EShadowLightType::Directional;
		ShadowLight.PositionWS = FVector::ZeroVector;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Bias = 0.000001f;
		ShadowLight.SlopeBias = 0.001f;
		ShadowLight.NormalBias = 0.0f;
		ShadowLight.Sharpen = 0.0f;

		uint32 CascadeCount = DirLight->GetCascadeCount();
		CascadeCount = (std::min)(CascadeCount, ShadowConfig::MaxDirCascade);

		TArray<float> FrustumSplits = FCasCade::CalculateCascadeSplits(CascadeCount, View.NearZ, View.FarZ, DirLight->GetSplitLambda());
		
		if (FrustumSplits.size() < 2)
		{
			return;
		}

		LightItem.CascadeSplits = FVector4(
			FrustumSplits.size() > 1 ? FrustumSplits[1] : 0.0f,
			FrustumSplits.size() > 2 ? FrustumSplits[2] : 0.0f,
			FrustumSplits.size() > 3 ? FrustumSplits[3] : 0.0f,
			FrustumSplits.size() > 4 ? FrustumSplits[4] : 0.0f
		);

		FVector UpVector = (std::abs(LightItem.DirectionWS.Z) > 0.999f) ? FVector::YAxisVector : FVector::ZAxisVector;

		FMatrix CameraInvView = View.InverseView;
		FMatrix CameraInvProj = View.InverseProjection;

		// NDC좌표상 각 꼭짓점
		FVector4 NDC_Corners[4] = {
			FVector4(-1.0f,  1.0f, 1.0f, 1.0f), FVector4(1.0f,  1.0f, 1.0f, 1.0f),
			FVector4(1.0f, -1.0f, 1.0f, 1.0f), FVector4(-1.0f, -1.0f, 1.0f, 1.0f)
		};

		// 절두체 4개의 변
		FVector ViewRays[4];
		for (int j = 0; j < 4; j++)
		{
			FVector4 ViewCorner = NDC_Corners[j] * CameraInvProj;
			float InvW = 1.0f / ViewCorner.W;
			FVector Ray = FVector(ViewCorner.X * InvW, ViewCorner.Y * InvW, ViewCorner.Z * InvW);

			ViewRays[j] = Ray * (1.0f / Ray.X);
		}
		
		// 절두체 별
		for (uint32 i = 0; i < CascadeCount; i++)
		{
			float NearSplit = FrustumSplits[i];
			float FarSplit = FrustumSplits[i + 1];

			FVector FrustumCornersWS[8];
			for (int j = 0; j < 4; j++)
			{
				FVector NearVS = ViewRays[j] * NearSplit;
				FVector FarVS = ViewRays[j] * FarSplit;

				FrustumCornersWS[j] = CameraInvView.TransformPosition(NearVS);
				FrustumCornersWS[j + 4] = CameraInvView.TransformPosition(FarVS);
			}

			FVector FrustumCenter = FVector::ZeroVector;
			for (int j = 0; j < 8; j++) { FrustumCenter += FrustumCornersWS[j]; }
			FrustumCenter /= 8.0f;

			float SphereRadius = 0.0f;
			for (int j = 0; j < 8; j++)
			{
				SphereRadius = (std::max)(SphereRadius, FVector::Dist(FrustumCenter, FrustumCornersWS[j]));
			}

			// 부동 소수점 오차
			SphereRadius = std::ceil(SphereRadius * 16.0f) / 16.0f;

			float BoxWidth = SphereRadius * 2.0f;
			float BoxHeight = SphereRadius * 2.0f;

			float WorldUnitsPerTexel = BoxWidth / ShadowConfig::DirShadowDepthResolution;

			// Texel 작업을 위함. Sanpping 현상 완화
			// 빛을 0, 0, 0으로 두고, 이를 기반으로 위치를 Texel화 -> floor를 이용
			FMatrix TempShadowView = FMatrix::MakeViewLookAtLH(FVector::ZeroVector, LightItem.DirectionWS, UpVector);
			FVector CenterLS = TempShadowView.TransformPosition(FrustumCenter);

			CenterLS.Y = std::floor(CenterLS.Y / WorldUnitsPerTexel) * WorldUnitsPerTexel;
			CenterLS.Z = std::floor(CenterLS.Z / WorldUnitsPerTexel) * WorldUnitsPerTexel;

			// 다시 절두체의 중심을 world세계로 변환. -> 이를 이용해서 View Proj행렬 계산.
			FVector SnappedCenterWS = TempShadowView.GetInverse().TransformPosition(CenterLS);
			FVector LightPosition = SnappedCenterWS;

			// -2000으로 두면 VSM의 빛샘현상이 너무 심하게 나타남.
			// float BoxNear = -SphereRadius - 2000.0f; 
			float BoxNear = -SphereRadius;
			float BoxFar = SphereRadius;

			FShadowViewRenderItem ViewItem;
			ViewItem.ProjectionType = EShadowProjectionType::Orthographic;
			ViewItem.PositionWS = FrustumCenter;
			ViewItem.NearZ = BoxNear;
			ViewItem.FarZ = BoxFar;
			

			float ResolutionScale = DirLight->GetShadowResolutionScale();
			uint32 RequestedResolution = QuantizeDiraShadowResolution(static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionScale));
			ViewItem.RequestedResolution = RequestedResolution;
			ViewItem.BiasParams = { DirLight->GetCascadeBias(i), DirLight->GetCascadeSlopeBias(i), 0.0f, 0.0f };
			ViewItem.View = FMatrix::MakeViewLookAtLH(LightPosition, LightPosition + LightItem.DirectionWS, UpVector);
			ViewItem.Projection = FMatrix::MakeOrthographicLH(BoxWidth, BoxHeight, BoxNear, BoxFar);
			ViewItem.ViewProjection = ViewItem.View * ViewItem.Projection;
			ViewItem.Viewport = {};

			AddDirShadowView(Inputs, ShadowLightIndex, ViewItem);
		}
	}

	void BuildPointShadowViews(FSceneLightingInputs& Inputs,const UPointLightComponent* Point, const FLocalLightRenderItem& LightItem,uint32 ShadowLightIndex, uint32 CubeArrayIndex)
	{
		static const FVector CubeFaceLook[6] = {{ 1, 0, 0 }, { -1, 0, 0 },	{ 0, 1, 0 }, { 0,-1, 0 },{0, 0, 1 },{ 0, 0, -1 },};
		static const FVector CubeFaceUp[6] = {{ 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },{ 0, 1, 0 }, { 0, 1, 0 },	};
		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];
		ShadowLight.PositionWS = LightItem.PositionWS;
		ShadowLight.Mobility = Point->GetMobility();
		ShadowLight.bCacheDirty = Point->IsCacheDirty();
		Point->ResetShadowCacheDirty();
		ShadowLight.Bias       = Point->GetShadowBias();
		ShadowLight.SlopeBias  = Point->GetShadowSlopeBias();
		ShadowLight.Sharpen    = Point->GetShadowSharpen();
		ShadowLight.CubeArrayIndex = CubeArrayIndex;

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ = (std::max)(LightItem.Range, NearZ + 0.001f);

		const uint32 BaseSlice = ShadowConfig::PointShadowSliceOffset + CubeArrayIndex * 6;

		for (uint32 F = 0; F < 6; ++F)
		{
			FShadowViewRenderItem View;
			View.ProjectionType = EShadowProjectionType::Perspective;
			View.PositionWS = LightItem.PositionWS;
			View.NearZ = NearZ;
			View.FarZ = FarZ;
			View.RequestedResolution = ShadowConfig::DefaultShadowMapResolution;

			View.View = FMatrix::MakeViewLookAtLH(
				LightItem.PositionWS,
				LightItem.PositionWS + CubeFaceLook[F],
				CubeFaceUp[F]);	

			View.Projection = FMatrix::MakePerspectiveFovLH(
				FMath::DegreesToRadians(90.0f), 1.0f, NearZ, FarZ);

			View.ViewProjection = View.View * View.Projection;
			View.FilterMode = EShadowFilterMode::Raw; 
			View.LightType = EShadowLightType::Point;

			AddPointShadowView(Inputs, ShadowLightIndex, BaseSlice + F, View);
		}

	}
	float ComputeShadowPriority(const FVector& LightPos, const FVector& CameraPos)
	{
		return (LightPos - CameraPos).SizeSquared();

	}

}


void FSceneCommandLightingBuilder::BuildLightingInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket&        Packet,
	const FViewContext&              View,
	FSceneViewData&                  OutSceneViewData) const
{
	(void)Packet;
	(void)View;
	FSceneLightingInputs& LightingInputs = OutSceneViewData.LightingInputs;
	LightingInputs.Clear();

	LightingInputs.Ambient.Color     = FVector::OneVector;
	LightingInputs.Ambient.Intensity = 0.0f;

	if (!BuildContext.World)
	{
		return;
	}


	//temp Condidtate colletion vector
	struct FPointShadowCandidate
	{
		const UPointLightComponent* PointLightl = nullptr;
		FLocalLightRenderItem LightItem;
		uint32 LocalLightIndex = 0;
		float SortKey = 0.0f;
	};
	std::vector<FPointShadowCandidate> ShadowCandidates;
	ShadowCandidates.reserve(16);

	FVector                     AmbientRadiance      = FVector::ZeroVector;
	float                       AmbientIntensitySum  = 0.0f;
	bool                        bHasAmbientLight     = false;
	bool                        bHasDirectionalLight = false;
	float                       StrongestDirectional = -1.0f;
	FDirectionalLightRenderItem DirectionalLightItem;

	const TArray<AActor*> Actors = BuildContext.World->GetAllActors();
	LightingInputs.LocalLights.reserve(Actors.size());

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || Component->IsPendingKill() || !Component->IsRegistered())
			{
				continue;
			}

			if (Component->IsA(UAmbientLightComponent::StaticClass()))
			{
				const UAmbientLightComponent* Ambient = static_cast<UAmbientLightComponent*>(Component);
				if (!Ambient->GetVisible())
				{
					continue;
				}

				const float EffectiveIntensity = Ambient->GetEffectiveIntensity();
				if (EffectiveIntensity <= 0.0f)
				{
					continue;
				}

				AmbientRadiance     += ToLightRGB(Ambient->GetColor()) * EffectiveIntensity;
				AmbientIntensitySum += EffectiveIntensity;
				bHasAmbientLight    = true;
				continue;
			}

			if (Component->IsA(UDirectionalLightComponent::StaticClass()))
			{
				const UDirectionalLightComponent* Directional = static_cast<UDirectionalLightComponent*>(Component);
				if (!Directional->GetVisible() || Directional->GetEffectiveIntensity() <= 0.0f)
				{
					continue;
				}

				const FDirectionalLightRenderItem Candidate = BuildDirectionalLight(Directional);
				if (!bHasDirectionalLight || Candidate.Intensity > StrongestDirectional)
				{
					DirectionalLightItem = Candidate;
					StrongestDirectional = Candidate.Intensity;
					bHasDirectionalLight = true;

					LightingInputs.DirShadowLights.clear();
					LightingInputs.DirShadowViews.clear();
					
					if (Directional->IsCastingShadows())
					{
						const uint32 ShadowLightIndex = AllocateDirShadowLight(LightingInputs, EShadowLightType::Directional, 0);
						if (ShadowLightIndex != UINT32_MAX)
						{
							BuildDirectionalShadowViews(LightingInputs, Directional, DirectionalLightItem, ShadowLightIndex, View);
						}
					}
				}
				continue;
			}

			if (Component->IsA(USpotLightComponent::StaticClass()))
			{
				
				const USpotLightComponent* Spot = static_cast<USpotLightComponent*>(Component);
				if (!Spot->GetVisible() || Spot->GetEffectiveIntensity() <= 0.0f || Spot->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				FLocalLightRenderItem LightItem       = BuildSpotLight(Spot);
				const uint32          LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());
				if (Spot->IsCastingShadows())
				{
					const uint32 ShadowLightIndex = AllocalteShadowLight(LightingInputs, EShadowLightType::Spot, LocalLightIndex);

					if (ShadowLightIndex != UINT32_MAX)
					{
						BuildSpotShadowViews(LightingInputs, Spot, LightItem, LocalLightIndex, ShadowLightIndex, View.ViewProjection);
						if (LightingInputs.ShadowLights[ShadowLightIndex].ViewCount > 0)
						{
							LightItem.ShadowIndex = ShadowLightIndex;
						}
					}
				}

				LightingInputs.LocalLights.push_back(LightItem);
				continue;
			}

			if (Component->IsA(UPointLightComponent::StaticClass()))
			{
				const UPointLightComponent* Point = static_cast<UPointLightComponent*>(Component);
				if (!Point->GetVisible() || Point->GetEffectiveIntensity() <= 0.0f
					|| Point->GetAttenuationRadius() <= 0.0f)
					continue;

				FLocalLightRenderItem LightItem = BuildPointLight(Point);
				const uint32 LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Point->IsCastingShadows())
				{
					// 후보로만 수집 등록은 정렬 후 2차 패스에서
					FPointShadowCandidate Candidate;
					Candidate.PointLightl = Point;
					Candidate.LightItem = LightItem;
					Candidate.LocalLightIndex = LocalLightIndex;
					Candidate.SortKey = ComputeShadowPriority(LightItem.PositionWS, OutSceneViewData.View.CameraPosition);
					ShadowCandidates.push_back(Candidate);
			
				}
				else
				{
					LightingInputs.LocalLights.push_back(LightItem);
				}
				continue;
			}
		}
	}
	std::sort(ShadowCandidates.begin(), ShadowCandidates.end(),
		[](const FPointShadowCandidate& A, const FPointShadowCandidate& B)
	{;
	return A.SortKey < B.SortKey;
	});

	uint32 PointCubeCounter = 0;

	for (FPointShadowCandidate& Candidate : ShadowCandidates)
	{
		FLocalLightRenderItem LightItem = Candidate.LightItem;

		if (PointCubeCounter < ShadowConfig::MaxPointShadowCubes)
		{
			const uint32 ShadowLightIndex = AllocalteShadowLight(
				LightingInputs, EShadowLightType::Point, Candidate.LocalLightIndex);

			if (ShadowLightIndex != UINT32_MAX)
			{
				BuildPointShadowViews(LightingInputs, Candidate.PointLightl, LightItem,
					ShadowLightIndex, PointCubeCounter);

				if (LightingInputs.ShadowLights[ShadowLightIndex].ViewCount == 6)
				{
					LightItem.ShadowIndex = ShadowLightIndex;
					++PointCubeCounter;
				}
			}
		}

		LightingInputs.LocalLights.push_back(LightItem);
	}
	if (bHasAmbientLight && AmbientIntensitySum > 0.0f)
	{
		LightingInputs.Ambient.Color     = AmbientRadiance / AmbientIntensitySum;
		LightingInputs.Ambient.Intensity = AmbientIntensitySum;
	}

	if (bHasDirectionalLight)
	{
		LightingInputs.DirectionalLights.push_back(DirectionalLightItem);
	}
}
