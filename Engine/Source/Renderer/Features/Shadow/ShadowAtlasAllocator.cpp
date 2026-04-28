#include "ShadowAtlasAllocator.h"

FShadowAtlasNode* FShadowAtlasNode::Insert(int RequiredSize)
{
	if (!IsLeaf())
	{
		// 큰 블록부터 안정적으로 탐색
		for (int i = 0; i < 4; ++i)
		{
			if (FShadowAtlasNode* Node = Children[i]->Insert(RequiredSize))
			{
				return Node;
			}
		}

		return nullptr;
	}

	if (bUsed)
	{
		return nullptr;
	}

	if (RequiredSize > Size)
	{
		return nullptr;
	}

	if (RequiredSize == Size)
	{
		bUsed = true;
		return this;
	}

	Split();
	return Children[0]->Insert(RequiredSize);
}

void FShadowAtlasNode::Split()
{
	const int HalfSize = Size / 2;

	Children[0] = std::make_unique<FShadowAtlasNode>(X, Y, HalfSize);
	Children[1] = std::make_unique<FShadowAtlasNode>(X + HalfSize, Y, HalfSize);
	Children[2] = std::make_unique<FShadowAtlasNode>(X, Y + HalfSize, HalfSize);
	Children[3] = std::make_unique<FShadowAtlasNode>(X + HalfSize, Y + HalfSize, HalfSize);
}

bool FShadowAtlasAllocator::Allocate(uint32 RequestedSize, FShadowAtlasAllocation& OutAllocation)
{
	OutAllocation = {};

	uint32 NormalizedSize = NormalizeSize(RequestedSize);
	if (NormalizedSize < Desc.MinAllocateSize)
	{
		return false;
	}

	const uint32 MinFallbackSize = std::max(
		Desc.MinAllocateSize,
		NormalizedSize >> Desc.MaxFallbackMipDrop
	);

	uint32 TrySize = NormalizedSize;

	while (TrySize >= MinFallbackSize)
	{
		if (FShadowAtlasNode* Node = RootNode->Insert(static_cast<int>(TrySize)))
		{
			OutAllocation.X = Node->X;
			OutAllocation.Y = Node->Y;
			OutAllocation.Size = Node->Size;
			OutAllocation.RequestedSize = NormalizedSize;
			OutAllocation.AllocatedSize = TrySize;
			return true;
		}

		TrySize >>= 1;
	}

	return false;
}

uint32 FShadowAtlasAllocator::NormalizeSize(uint32 RequestedSize) const
{
	RequestedSize = std::min(RequestedSize, Desc.AtlasSize);
	RequestedSize = RoundDownPowerOfTwo(RequestedSize);
	return RequestedSize;
};