#include "Renderer/UI/Screen/ScreenUIRendererUtils.h"

#include "Math/LinearColor.h"
#include "Renderer/Mesh/Vertex.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace
{
    static FVertex MakeVertex(float X, float Y, float U, float V, const FVector4& Color)
    {
        FVertex Out{};
        Out.Position = FVector(X, Y, 0.0f);
        Out.Color = Color;
        Out.Normal = FVector(0.0f, 0.0f, 1.0f);
        Out.UV = FVector2(U, V);
        return Out;
    }
}

namespace ScreenUIRendererUtils
{
    FVector4 ToColor(uint32 C)
    {
        const float A = ((C >> 24) & 0xFF) / 255.0f;
        const float R = ((C >> 16) & 0xFF) / 255.0f;
        const float G = ((C >> 8) & 0xFF) / 255.0f;
        const float B = (C & 0xFF) / 255.0f;
        return FLinearColor::SRGBToLinear(FVector4(R, G, B, A));
    }

    void ConvertTextMeshToScreenSpace(FDynamicMesh& Mesh)
    {
        float MinX = FLT_MAX;
        float MinY = FLT_MAX;
        for (FVertex& Vertex : Mesh.Vertices)
        {
            const float ScreenX = Vertex.Position.Y;
            const float ScreenY = -Vertex.Position.Z;
            Vertex.Position = FVector(ScreenX, ScreenY, 0.0f);
            MinX = (std::min)(MinX, Vertex.Position.X);
            MinY = (std::min)(MinY, Vertex.Position.Y);
        }

        for (FVertex& Vertex : Mesh.Vertices)
        {
            Vertex.Position.X -= MinX;
            Vertex.Position.Y -= MinY;
        }
    }

    bool HasClip(const FUIDrawElement& Element)
    {
        return Element.bHasClipRect && Element.ClipRect.IsValid();
    }

    FUIRect IntersectUIRect(const FUIRect& A, const FUIRect& B)
    {
        const float X0 = (std::max)(A.X, B.X);
        const float Y0 = (std::max)(A.Y, B.Y);
        const float X1 = (std::min)(A.X + A.Width, B.X + B.Width);
        const float Y1 = (std::min)(A.Y + A.Height, B.Y + B.Height);

        FUIRect Out;
        Out.X = X0;
        Out.Y = Y0;
        Out.Width = (std::max)(0.0f, X1 - X0);
        Out.Height = (std::max)(0.0f, Y1 - Y0);
        return Out;
    }

    bool ResolveClippedRect(const FUIDrawElement& Element, FUIRect& OutRect)
    {
        OutRect = Element.Rect;
        if (!OutRect.IsValid())
        {
            return false;
        }

        if (!HasClip(Element))
        {
            return true;
        }

        OutRect = IntersectUIRect(OutRect, Element.ClipRect);
        return OutRect.IsValid();
    }

    void AppendQuad(
        FDynamicMesh& Mesh,
        float X0,
        float Y0,
        float X1,
        float Y1,
        float U0,
        float V0,
        float U1,
        float V1,
        const FVector4& Color)
    {
        if (X1 <= X0 || Y1 <= Y0)
        {
            return;
        }

        const uint32 Base = static_cast<uint32>(Mesh.Vertices.size());
        Mesh.Vertices.push_back(MakeVertex(X0, Y0, U0, V0, Color));
        Mesh.Vertices.push_back(MakeVertex(X1, Y0, U1, V0, Color));
        Mesh.Vertices.push_back(MakeVertex(X1, Y1, U1, V1, Color));
        Mesh.Vertices.push_back(MakeVertex(X0, Y1, U0, V1, Color));

        Mesh.Indices.push_back(Base + 0);
        Mesh.Indices.push_back(Base + 1);
        Mesh.Indices.push_back(Base + 2);
        Mesh.Indices.push_back(Base + 0);
        Mesh.Indices.push_back(Base + 2);
        Mesh.Indices.push_back(Base + 3);
    }

    void AppendClippedQuad(
        FDynamicMesh& Mesh,
        float X0,
        float Y0,
        float X1,
        float Y1,
        float U0,
        float V0,
        float U1,
        float V1,
        const FVector4& Color,
        const FUIRect* ClipRect)
    {
        if (X1 <= X0 || Y1 <= Y0)
        {
            return;
        }

        float CX0 = X0;
        float CY0 = Y0;
        float CX1 = X1;
        float CY1 = Y1;

        if (ClipRect)
        {
            const float NX0 = (std::max)(X0, ClipRect->X);
            const float NY0 = (std::max)(Y0, ClipRect->Y);
            const float NX1 = (std::min)(X1, ClipRect->X + ClipRect->Width);
            const float NY1 = (std::min)(Y1, ClipRect->Y + ClipRect->Height);
            if (NX1 <= NX0 || NY1 <= NY0)
            {
                return;
            }

            CX0 = NX0;
            CY0 = NY0;
            CX1 = NX1;
            CY1 = NY1;
        }

        const float Width = X1 - X0;
        const float Height = Y1 - Y0;

        const float TU0 = U0 + ((CX0 - X0) / Width) * (U1 - U0);
        const float TU1 = U0 + ((CX1 - X0) / Width) * (U1 - U0);
        const float TV0 = V0 + ((CY0 - Y0) / Height) * (V1 - V0);
        const float TV1 = V0 + ((CY1 - Y0) / Height) * (V1 - V0);

        AppendQuad(Mesh, CX0, CY0, CX1, CY1, TU0, TV0, TU1, TV1, Color);
    }

    bool MeasureMeshBounds(const FDynamicMesh& Mesh, float& OutMaxX, float& OutMaxY)
    {
        if (Mesh.Vertices.empty())
        {
            OutMaxX = 0.0f;
            OutMaxY = 0.0f;
            return false;
        }

        OutMaxX = 0.0f;
        OutMaxY = 0.0f;
        for (const FVertex& Vertex : Mesh.Vertices)
        {
            OutMaxX = (std::max)(OutMaxX, Vertex.Position.X);
            OutMaxY = (std::max)(OutMaxY, Vertex.Position.Y);
        }
        return true;
    }

    int32 MakeDepthSortKey(float Depth)
    {
        if (!std::isfinite(Depth))
        {
            return 0;
        }

        constexpr double DepthScale = 1024.0;
        const double Scaled = static_cast<double>(Depth) * DepthScale;
        const double MinKey = static_cast<double>((std::numeric_limits<int32>::min)());
        const double MaxKey = static_cast<double>((std::numeric_limits<int32>::max)());
        const double Clamped = (std::max)(MinKey, (std::min)(Scaled, MaxKey));
        return static_cast<int32>(std::llround(Clamped));
    }
}
