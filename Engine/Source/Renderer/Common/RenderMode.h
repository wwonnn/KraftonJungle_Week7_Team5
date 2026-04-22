#pragma once

#include "CoreMinimal.h"

enum class ERenderMode : uint8
{
	Lit_Gouraud = 0,
	Lit_Lambert,
	Lit_Phong,
	Lit_Toon,
	Unlit,
	Wireframe,
	SceneDepth,
	WorldNormal,
	LightCullingHeatmap
};
