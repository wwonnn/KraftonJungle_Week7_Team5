#pragma once
#include <memory>
#include <algorithm>
#include "CoreMinimal.h"

struct FShadowAtlasAllocation
{
	int X = 0;
	int Y = 0;
	int Size = 0;
	uint32 RequestedSize = 0;
	uint32 AllocatedSize = 0;

	bool IsValid() const { return AllocatedSize > 0; }
};

struct FShadowAtlasNode
{
	int X = 0;
	int Y = 0;
	int Size = 0;
	bool bUsed = false;
	std::unique_ptr<FShadowAtlasNode> Children[4] = { nullptr, nullptr, nullptr, nullptr };

	FShadowAtlasNode(int InX, int InY, int InSize)
		: X(InX), Y(InY), Size(InSize)
	{
	}

	bool IsLeaf() const
	{
		return Children[0] == nullptr;
	}

	FShadowAtlasNode* Insert(int RequiredSize);

	void Split();
};

struct FShadowAtlasAllocatorDesc
{
	uint32 AtlasSize = 4096;
	uint32 MinAllocateSize = 128;
	uint32 MaxFallbackMipDrop = 1; // 1이면 1024 실패 시 512까지만 시도
};

class FShadowAtlasAllocator
{
public:
	explicit FShadowAtlasAllocator(const FShadowAtlasAllocatorDesc& InDesc)
		: Desc(InDesc)
	{
		Reset();
	}

	explicit FShadowAtlasAllocator(uint32 InAtlasSize)
	{
		Desc.AtlasSize = InAtlasSize;
		Reset();
	}

	void Reset()
	{
		RootNode = std::make_unique<FShadowAtlasNode>(0, 0, static_cast<int>(Desc.AtlasSize));
	}

	bool Allocate(uint32 RequestedSize, FShadowAtlasAllocation& OutAllocation);

	uint32 GetAtlasSize() const
	{
		return Desc.AtlasSize;
	}

	uint32 GetMinAllocateSize() const
	{
		return Desc.MinAllocateSize;
	}

private:
	static uint32 RoundDownPowerOfTwo(uint32 Value)
	{
		if (Value == 0)
		{
			return 0;
		}

		uint32 Result = 1;
		while ((Result << 1) <= Value)
		{
			Result <<= 1;
		}

		return Result;
	}

	uint32 NormalizeSize(uint32 RequestedSize) const;


private:
	FShadowAtlasAllocatorDesc Desc;
	std::unique_ptr<FShadowAtlasNode> RootNode;
};

