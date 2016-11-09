#include "stdafx.h"
#include "Terrain/TerrainQuadTreeNode.h"
#include "Terrain/Terrain.h"
#include "Buffer.h"
#include "Mesh.h"
#include "Entity.h"
#include "MaterialManager.h"
#include "Material.h"
#include "Renderer.h"
#include "Camera.h"
#include "SceneManager.h"

namespace Neo
{
	unsigned short TerrainQuadTreeNode::POSITION_BUFFER = 0;
	unsigned short TerrainQuadTreeNode::DELTA_BUFFER = 1;

	//---------------------------------------------------------------------
	TerrainQuadTreeNode::TerrainQuadTreeNode(Terrain* terrain, 
		TerrainQuadTreeNode* parent, uint16 xoff, uint16 yoff, uint16 size, 
		uint16 lod, uint16 depth, uint16 quadrant)
		: mTerrain(terrain)
		, mParent(parent)
		, mOffsetX(xoff)
		, mOffsetY(yoff)
		, mBoundaryX(xoff + size)
		, mBoundaryY(yoff + size)
		, mSize(size)
		, mBaseLod(lod)
		, mDepth(depth)
		, mQuadrant(quadrant)
		, mBoundingRadius(0)
        , mCurrentLod(-1)
		, mLodTransition(0)
		, mChildWithMaxHeightDelta(0)
		, mSelfOrChildRendered(false)
		, mNodeWithVertexData(0)
		, mVertexDataRecord(0)
		, mEntity(nullptr)
	{
		// 四叉树一直拆分,直到满足最大细节lod
		if (terrain->getMaxBatchSize() < size)
		{
			uint16 childSize = (uint16)(((size - 1) * 0.5f) + 1);
			uint16 childOff = childSize - 1;
			uint16 childLod = lod - 1; // LOD levels decrease down the tree (higher detail)
			uint16 childDepth = depth + 1;
			// create children
			mChildren[0] = new TerrainQuadTreeNode(terrain, this, xoff, yoff, childSize, childLod, childDepth, 0);
			mChildren[1] = new TerrainQuadTreeNode(terrain, this, xoff + childOff, yoff, childSize, childLod, childDepth, 1);
			mChildren[2] = new TerrainQuadTreeNode(terrain, this, xoff, yoff + childOff, childSize, childLod, childDepth, 2);
			mChildren[3] = new TerrainQuadTreeNode(terrain, this, xoff + childOff, yoff + childOff, childSize, childLod, childDepth, 3);

			LodLevel* ll = new LodLevel();
			// non-leaf nodes always render with minBatchSize vertices
			ll->batchSize = terrain->getMinBatchSize();
			ll->maxHeightDelta = 0;
			ll->calcMaxHeightDelta = 0;
			mLodLevels.push_back(ll);

		}
		else
		{
			// No children
			memset(mChildren, 0, sizeof(TerrainQuadTreeNode*) * 4);

			// this is a leaf node and may have internal LODs of its own
			uint16 ownLod = terrain->getNumLodLevelsPerLeaf();
			_AST (lod == (ownLod - 1) && "The lod passed in should reflect the number of lods in a leaf");

			// leaf nodes always have a base LOD of 0, because they're always handling
			// the highest level of detail
			mBaseLod = 0;
			// leaf nodes render from max batch size to min batch size
			uint16 sz = terrain->getMaxBatchSize();

			while (ownLod--)
			{
				LodLevel* ll = new LodLevel();
				ll->batchSize = sz;
				ll->maxHeightDelta = 0;
				ll->calcMaxHeightDelta = 0;
				mLodLevels.push_back(ll);
				if (ownLod)
					sz = (uint16)(((sz - 1) * 0.5) + 1);
			}
			
			_AST(sz == terrain->getMinBatchSize());

		}
		
		// Local centre calculation
		// Because of pow2 + 1 there is always a middle point
		uint16 midoffset = (size - 1) / 2;
		uint16 midpointx = mOffsetX + midoffset;
		uint16 midpointy = mOffsetY + midoffset;
		// derive the local centre, but give it a height of 0
		// TODO - what if we actually centred this at the terrain height at this point?
		// would this be better?
		mTerrain->getPoint(midpointx, midpointy, 0, &mLocalCentre);
	}
	//---------------------------------------------------------------------
	TerrainQuadTreeNode::~TerrainQuadTreeNode()
	{
		for (int i = 0; i < 4; ++i)
			delete mChildren[i];

		destroyCpuVertexData();
		destroyGpuVertexData();
		destroyGpuIndexData();

		for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i)
			delete *i;

		delete mVertexDataRecord;
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::isLeaf() const
	{
		return mChildren[0] == 0;
	}
	//---------------------------------------------------------------------
	TerrainQuadTreeNode* TerrainQuadTreeNode::getChild(unsigned short child) const
	{
		if (isLeaf() || child >= 4)
			return 0;

		return mChildren[child];
	}
	//---------------------------------------------------------------------
	TerrainQuadTreeNode* TerrainQuadTreeNode::getParent() const
	{
		return mParent;
	}
	//---------------------------------------------------------------------
	Terrain* TerrainQuadTreeNode::getTerrain() const
	{
		return mTerrain;
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::prepare()
	{
		if (!isLeaf())
		{
			for (int i = 0; i < 4; ++i)
				mChildren[i]->prepare();
		}

	}
	//---------------------------------------------------------------------
	//void TerrainQuadTreeNode::prepare(StreamSerialiser& stream)
	//{
	//	// load LOD data we need
	//	for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i)
	//	{
	//		LodLevel* ll = *i;
	//		// only read 'calc' and then copy to final (separation is only for
	//		// real-time calculation
	//		// Basically this is what finaliseHeightDeltas does in calc path
	//		stream.read(&ll->calcMaxHeightDelta);
	//		ll->maxHeightDelta = ll->calcMaxHeightDelta;
	//		ll->lastCFactor = 0;
	//	}

	//	if (!isLeaf())
	//	{
	//		for (int i = 0; i < 4; ++i)
	//			mChildren[i]->prepare(stream);
	//	}

	//	// If this is the root, do the post delta calc to finish
	//	if (!mParent)
	//	{
	//		Rect rect;
	//		rect.top = mOffsetY; rect.bottom = mBoundaryY;
	//		rect.left = mOffsetX; rect.right = mBoundaryX;
	//		postDeltaCalculation(rect);
	//	}
	//}
	////---------------------------------------------------------------------
	//void TerrainQuadTreeNode::save(StreamSerialiser& stream)
	//{
	//	// save LOD data we need
	//	for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i)
	//	{
	//		LodLevel* ll = *i;
	//		stream.write(&ll->maxHeightDelta);
	//	}

	//	if (!isLeaf())
	//	{
	//		for (int i = 0; i < 4; ++i)
	//			mChildren[i]->save(stream);
	//	}
	//}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::load()
	{
		loadSelf();

		if (!isLeaf())
			for (int i = 0; i < 4; ++i)
				mChildren[i]->load();
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::load(uint16 treeDepthStart, uint16 treeDepthEnd)
	{
		if (mDepth >= treeDepthEnd)
			return ;

		if (mDepth >= treeDepthStart && mDepth < treeDepthEnd && mNodeWithVertexData)
			loadSelf();

		if (!isLeaf())
			for (int i = 0; i < 4; ++i)
				mChildren[i]->load(treeDepthStart, treeDepthEnd);
	}
	void TerrainQuadTreeNode::loadSelf()
	{
		createGpuVertexData();
		createGpuIndexData();
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::unload()
	{
		if (!isLeaf())
			for (int i = 0; i < 4; ++i)
				mChildren[i]->unload();

		destroyGpuVertexData();

		SAFE_DELETE(mEntity);
	}

	void TerrainQuadTreeNode::unload(uint16 treeDepthStart, uint16 treeDepthEnd)
	{
		if (mDepth >= treeDepthEnd)
			return ;

		if (!isLeaf())
			for (int i = 0; i < 4; ++i)
				mChildren[i]->unload(treeDepthStart, treeDepthEnd);

		if (mDepth >= treeDepthStart && mDepth < treeDepthEnd)
		{
			destroyGpuVertexData();
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::unprepare()
	{
		if (!isLeaf())
			for (int i = 0; i < 4; ++i)
				mChildren[i]->unprepare();

		destroyCpuVertexData();
	}
	//---------------------------------------------------------------------
	uint16 TerrainQuadTreeNode::getLodCount() const
	{
		return static_cast<uint16>(mLodLevels.size());
	}
	//---------------------------------------------------------------------
	const TerrainQuadTreeNode::LodLevel* TerrainQuadTreeNode::getLodLevel(uint16 lod)
	{
		_AST(lod < mLodLevels.size());

		return mLodLevels[lod];
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::preDeltaCalculation(const Rect& rect)
	{
		if (rect.left <= mBoundaryX || rect.right > mOffsetX
			|| rect.top <= mBoundaryY || rect.bottom > mOffsetY)
		{
			// relevant to this node (overlaps)

			// if the rect covers the whole node, reset the max height
			// this means that if you recalculate the deltas progressively, end up keeping
			// a max height that's no longer the case (ie more conservative lod), 
			// but that's the price for not recaculating the whole node. If a 
			// complete recalculation is required, just dirty the entire node. (or terrain)

			// Note we use the 'calc' field here to avoid interfering with any
			// ongoing LOD calculations (this can be in the background)

			if (rect.left <= mOffsetX && rect.right > mBoundaryX 
				&& rect.top <= mOffsetY && rect.bottom > mBoundaryY)
			{
				for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i)
					(*i)->calcMaxHeightDelta = 0.0;
			}

			// pass on to children
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
					mChildren[i]->preDeltaCalculation(rect);

			}
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::notifyDelta(uint16 x, uint16 y, uint16 lod, float delta)
	{
		if (x >= mOffsetX && x < mBoundaryX 
			&& y >= mOffsetY && y < mBoundaryY)
		{
			// within our bounds, check it's our LOD level
			if (lod >= mBaseLod && lod < mBaseLod + mLodLevels.size())
			{
				// Apply the delta to all LODs equal or lower detail to lod
				LodLevelList::iterator i = mLodLevels.begin();
				std::advance(i, lod - mBaseLod);
				(*i)->calcMaxHeightDelta = std::max((*i)->calcMaxHeightDelta, delta);
			}

			// pass on to children
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
				{
					mChildren[i]->notifyDelta(x, y, lod, delta);
                                }
			}

		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::postDeltaCalculation(const Rect& rect)
	{
		if (rect.left <= mBoundaryX || rect.right > mOffsetX
			|| rect.top <= mBoundaryY || rect.bottom > mOffsetY)
		{
			// relevant to this node (overlaps)

			// each non-leaf node should know which of its children transitions
			// to the lower LOD level last, because this is the one which controls
			// when the parent takes over
			if (!isLeaf())
			{
				float maxChildDelta = -1;
				TerrainQuadTreeNode* childWithMaxHeightDelta = 0;
				for (int i = 0; i < 4; ++i)
				{
					TerrainQuadTreeNode* child = mChildren[i];

					child->postDeltaCalculation(rect);

					float childDelta = child->getLodLevel(child->getLodCount()-1)->calcMaxHeightDelta;
					if (childDelta > maxChildDelta)
					{
						childWithMaxHeightDelta = child;
						maxChildDelta = childDelta;
					}

				}

				// make sure that our highest delta value is greater than all children's
				// otherwise we could have some crossover problems
				// for a non-leaf, there is only one LOD level
				mLodLevels[0]->calcMaxHeightDelta = std::max(mLodLevels[0]->calcMaxHeightDelta, maxChildDelta * (float)1.05);
				mChildWithMaxHeightDelta = childWithMaxHeightDelta;

			}
			else
			{
				// make sure own LOD levels delta values ascend
				for (size_t i = 0; i < mLodLevels.size() - 1; ++i)
				{
					// the next LOD after this one should have a higher delta
					// otherwise it won't come into affect further back like it should!
					mLodLevels[i+1]->calcMaxHeightDelta = 
						std::max(mLodLevels[i+1]->calcMaxHeightDelta, 
							mLodLevels[i]->calcMaxHeightDelta * (float)1.05);
				}

			}
		}

	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::finaliseDeltaValues(const Rect& rect)
	{
		if (rect.left <= mBoundaryX || rect.right > mOffsetX
			|| rect.top <= mBoundaryY || rect.bottom > mOffsetY)
		{
			// relevant to this node (overlaps)

			// Children
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
				{
					TerrainQuadTreeNode* child = mChildren[i];
					child->finaliseDeltaValues(rect);
				}

			}

			// Self
			for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i)
			{
				// copy from 'calc' area to runtime value
				(*i)->maxHeightDelta = (*i)->calcMaxHeightDelta;
				// also trash stored cfactor
				(*i)->lastCFactor = 0;
			}

		}

	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::assignVertexData(uint16 treeDepthStart, 
		uint16 treeDepthEnd, uint16 resolution, uint32 sz)
	{
		_AST(treeDepthStart >= mDepth && "Should not be calling this");

		if (mDepth == treeDepthStart)
		{
			// we own this vertex data
			mNodeWithVertexData = this;
			if (!mVertexDataRecord)
				mVertexDataRecord = new VertexDataRecord(resolution, sz, treeDepthEnd - treeDepthStart);

			createCpuVertexData();

			// pass on to children
			if (!isLeaf() && treeDepthEnd > (mDepth + 1)) // treeDepthEnd is exclusive, and this is children
			{
				for (int i = 0; i < 4; ++i)
					mChildren[i]->useAncestorVertexData(this, treeDepthEnd, resolution);

			}
		}
		else
		{
			_AST(!isLeaf() && "No more levels below this!");

			for (int i = 0; i < 4; ++i)
				mChildren[i]->assignVertexData(treeDepthStart, treeDepthEnd, resolution, sz);
			
		}

	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::useAncestorVertexData(TerrainQuadTreeNode* owner, uint16 treeDepthEnd, uint16 resolution)
	{
		mNodeWithVertexData = owner; 
		mVertexDataRecord = 0;

		if (!isLeaf() && treeDepthEnd > (mDepth + 1)) // treeDepthEnd is exclusive, and this is children
		{
			for (int i = 0; i < 4; ++i)
				mChildren[i]->useAncestorVertexData(owner, treeDepthEnd, resolution);

		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::updateVertexData(bool positions, bool deltas, 
		const Rect& rect, bool cpuData)
	{
		if (rect.left <= mBoundaryX || rect.right > mOffsetX
			|| rect.top <= mBoundaryY || rect.bottom > mOffsetY)
		{
			// Do we have vertex data?
			if (mVertexDataRecord)
			{
				// Trim to our bounds
				Rect updateRect(mOffsetX, mOffsetY, mBoundaryX, mBoundaryY);
				updateRect.left = std::max(updateRect.left, rect.left);
				updateRect.right = std::min(updateRect.right, rect.right);
				updateRect.top = std::max(updateRect.top, rect.top);
				updateRect.bottom = std::min(updateRect.bottom, rect.bottom);

				// update the GPU buffer directly
				// TODO: do we have no use for CPU vertex data after initial load?
				// if so, destroy it to free RAM, this should be fast enough to 
				// to direct
				if(!cpuData)
				{
					if(mVertexDataRecord->gpuPosVertexBuf == NULL) 
						createGpuVertexData();
				}

				updateVertexBuffer(updateRect);
			}

			// pass on to children
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
				{
					mChildren[i]->updateVertexData(positions, deltas, rect, cpuData);

					// merge bounds from children
					AABB childBox = mChildren[i]->getAABB();
					// this box is relative to child centre
					VEC3 boxoffset = mChildren[i]->getLocalCentre() - getLocalCentre();
					childBox.m_minCorner = childBox.m_minCorner + boxoffset;
					childBox.m_maxCorner = childBox.m_maxCorner + boxoffset;
					mAABB.Merge(childBox);
				}

			}
		}

	}
	//---------------------------------------------------------------------
	const TerrainQuadTreeNode::VertexDataRecord* TerrainQuadTreeNode::getVertexDataRecord() const
	{
		return mNodeWithVertexData ? mNodeWithVertexData->mVertexDataRecord : 0;
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::createCpuVertexData()
	{
		if (mVertexDataRecord)
		{
			destroyCpuVertexData();

			// Calculate number of vertices
			// Base geometry size * size
			size_t baseNumVerts = (size_t)(mVertexDataRecord->size * mVertexDataRecord->size);
			size_t numVerts = baseNumVerts;
			// Now add space for skirts
			// Skirts will be rendered as copies of the edge vertices translated downwards
			// Some people use one big fan with only 3 vertices at the bottom, 
			// but this requires creating them much bigger that necessary, meaning
			// more unnecessary overdraw, so we'll use more vertices 
			// You need 2^levels + 1 rows of full resolution (max 129) vertex copies, plus
			// the same number of columns. There are common vertices at intersections
			uint16 levels = mVertexDataRecord->treeLevels;
			mVertexDataRecord->numSkirtRowsCols = (uint16)(pow(2, levels) + 1);
			mVertexDataRecord->skirtRowColSkip = (mVertexDataRecord->size - 1) / (mVertexDataRecord->numSkirtRowsCols - 1);
			numVerts += mVertexDataRecord->size * mVertexDataRecord->numSkirtRowsCols;
			numVerts += mVertexDataRecord->size * mVertexDataRecord->numSkirtRowsCols;

			// manually create CPU-side buffer
			const uint32 nVertSizePos = sizeof(uint16) * 2 + sizeof(float);
			const uint32 nVertSizeDelta = sizeof(float) * 2;

			mVertexDataRecord->cpuVertexPosData = new char[nVertSizePos * numVerts];
			mVertexDataRecord->cpuVertexDeltaData = new float[nVertSizeDelta * numVerts];
			mVertexDataRecord->cpuVertexCount= numVerts;

			Rect updateRect(mOffsetX, mOffsetY, mBoundaryX, mBoundaryY);
			updateVertexBuffer(updateRect);
			mVertexDataRecord->gpuVertexDataDirty = true;
		}
	}
	//----------------------------------------------------------------------
	void TerrainQuadTreeNode::updateVertexBuffer(const Rect& rect)
	{
		_AST (rect.left >= mOffsetX && rect.right <= mBoundaryX && 
			rect.top >= mOffsetY && rect.bottom <= mBoundaryY);

		// potentially reset our bounds depending on coverage of the update
		resetBounds(rect);

		// Main data
		uint16 inc = (mTerrain->getSize()-1) / (mVertexDataRecord->resolution-1);
		long destOffsetX = rect.left <= mOffsetX ? 0 : (rect.left - mOffsetX) / inc;
		long destOffsetY = rect.top <= mOffsetY ? 0 : (rect.top - mOffsetY) / inc;

		// Fill the buffers
		float uvScale = 1.0f / (mTerrain->getSize() - 1);
		const float* pBaseHeight = mTerrain->getHeightData(rect.left, rect.top);
		const float* pBaseDelta = mTerrain->getDeltaData(rect.left, rect.top);
		uint16 rowskip = mTerrain->getSize() * inc;
		uint16 destPosRowSkip = 0, destDeltaRowSkip = 0;
		unsigned char* pRootPosBuf = 0;
		unsigned char* pRootDeltaBuf = 0;
		unsigned char* pRowPosBuf = 0;
		unsigned char* pRowDeltaBuf = 0;
		const uint32 nVertSizePos = sizeof(uint16) * 2 + sizeof(float);
		const uint32 nVertSizeDelta = sizeof(float) * 2;

		if (mVertexDataRecord->cpuVertexPosData)
		{
			destPosRowSkip = mVertexDataRecord->size * nVertSizePos;
			pRootPosBuf = static_cast<unsigned char*>(mVertexDataRecord->cpuVertexPosData);
			pRowPosBuf = pRootPosBuf;
			// skip dest buffer in by left/top
			pRowPosBuf += destOffsetY * destPosRowSkip + destOffsetX * nVertSizePos;
		}
		if (mVertexDataRecord->cpuVertexDeltaData)
		{
			destDeltaRowSkip = mVertexDataRecord->size * nVertSizeDelta;
			pRootDeltaBuf = static_cast<unsigned char*>(mVertexDataRecord->cpuVertexDeltaData);
			pRowDeltaBuf = pRootDeltaBuf;
			// skip dest buffer in by left/top
			pRowDeltaBuf += destOffsetY * destDeltaRowSkip + destOffsetX * nVertSizeDelta;
		}
		VEC3 pos;
		
		for (uint16 y = rect.top; y < rect.bottom; y += inc)
		{
			const float* pHeight = pBaseHeight;
			const float* pDelta = pBaseDelta;
			float* pPosBuf = static_cast<float*>(static_cast<void*>(pRowPosBuf));
			float* pDeltaBuf = static_cast<float*>(static_cast<void*>(pRowDeltaBuf));
			for (uint16 x = rect.left; x < rect.right; x += inc)
			{
				if (pPosBuf)
				{
					mTerrain->getPoint(x, y, *pHeight, &pos);

					// Update bounds *before* making relative
					mergeIntoBounds(x, y, pos);
					// relative to local centre
					pos -= mLocalCentre;
			
					writePosVertex(x, y, *pHeight, pos, uvScale, &pPosBuf);
					pHeight += inc;
					

				}

				if (pDeltaBuf)
				{
					// delta, and delta LOD threshold
					// we want delta to apply to LODs no higher than this value
					// at runtime this will be combined with a per-renderable parameter
					// to ensure we only apply morph to the correct LOD
					writeDeltaVertex(x, y, *pDelta, 
						(float)mTerrain->getLODLevelWhenVertexEliminated(x, y) - 1.0f, 
						&pDeltaBuf);
					pDelta += inc;

				}

				
			}
			pBaseHeight += rowskip;
			pBaseDelta += rowskip;
			if (pRowPosBuf)
				pRowPosBuf += destPosRowSkip;
			if (pRowDeltaBuf)
				pRowDeltaBuf += destDeltaRowSkip;

		}

		// Skirts now
		// skirt spacing based on top-level resolution (* inc to cope with resolution which is not the max)
		uint16 skirtSpacing = mVertexDataRecord->skirtRowColSkip * inc;
		VEC3 skirtOffset;
		mTerrain->getVector(0, 0, -mTerrain->getSkirtSize(), &skirtOffset);

		// skirt rows
		// clamp rows to skirt spacing (round up)
		long skirtStartX = rect.left;
		long skirtStartY = rect.top;
		// for rows, clamp Y to skirt frequency, X to inc (LOD resolution vs top)
		if (skirtStartY % skirtSpacing)
			skirtStartY += skirtSpacing - (skirtStartY % skirtSpacing);
		if (skirtStartX % inc)
			skirtStartX += inc - (skirtStartX % inc);
		skirtStartY = std::max(skirtStartY, (long)mOffsetY);
		pBaseHeight = mTerrain->getHeightData(skirtStartX, skirtStartY);

		if (mVertexDataRecord->cpuVertexPosData)
		{
			// position dest buffer just after the main vertex data
			pRowPosBuf = pRootPosBuf + nVertSizePos * mVertexDataRecord->size * mVertexDataRecord->size;
			// move it onwards to skip the skirts we don't need to update
			pRowPosBuf += destPosRowSkip * ((skirtStartY - mOffsetY) / skirtSpacing);
			pRowPosBuf += nVertSizePos * (skirtStartX - mOffsetX) / inc;
		}
		if (mVertexDataRecord->cpuVertexDeltaData)
		{
			// position dest buffer just after the main vertex data
			pRowDeltaBuf = pRootDeltaBuf + nVertSizeDelta * mVertexDataRecord->size * mVertexDataRecord->size;
			// move it onwards to skip the skirts we don't need to update
			pRowDeltaBuf += destDeltaRowSkip * (skirtStartY - mOffsetY) / skirtSpacing;
			pRowDeltaBuf += nVertSizeDelta * (skirtStartX - mOffsetX) / inc;
		}
		for (uint16 y = skirtStartY; y < rect.bottom; y += skirtSpacing)
		{
			const float* pHeight = pBaseHeight;
			float* pPosBuf = static_cast<float*>(static_cast<void*>(pRowPosBuf));
			float* pDeltaBuf = static_cast<float*>(static_cast<void*>(pRowDeltaBuf));
			for (uint16 x = skirtStartX; x < rect.right; x += inc)
			{
				if (mVertexDataRecord->cpuVertexPosData)
				{
					mTerrain->getPoint(x, y, *pHeight, &pos);
					// relative to local centre
					pos -= mLocalCentre;
					pos += skirtOffset;
					writePosVertex(x, y, *pHeight - mTerrain->getSkirtSize(), pos, uvScale, &pPosBuf);

					pHeight += inc;

					

				}

				if (mVertexDataRecord->cpuVertexDeltaData)
				{
					// delta (none)
					// delta threshold (irrelevant)
					writeDeltaVertex(x, y, 0, 99, &pDeltaBuf);
				}
			}
			pBaseHeight += mTerrain->getSize() * skirtSpacing;
			if (pRowPosBuf)
				pRowPosBuf += destPosRowSkip;
			if (pRowDeltaBuf)
				pRowDeltaBuf += destDeltaRowSkip;
		}

		// skirt cols
		// clamp cols to skirt spacing (round up)
		skirtStartX = rect.left;
		if (skirtStartX % skirtSpacing)
			skirtStartX += skirtSpacing - (skirtStartX % skirtSpacing);
		// clamp Y to inc (LOD resolution vs top)
		skirtStartY = rect.top;
		if (skirtStartY % inc)
			skirtStartY += inc - (skirtStartY % inc);
		skirtStartX = std::max(skirtStartX, (long)mOffsetX);

		if (mVertexDataRecord->cpuVertexPosData)
		{
			// position dest buffer just after the main vertex data and skirt rows
			pRowPosBuf = pRootPosBuf + nVertSizePos * mVertexDataRecord->size * mVertexDataRecord->size;
			// skip the row skirts
			pRowPosBuf += mVertexDataRecord->numSkirtRowsCols * mVertexDataRecord->size * nVertSizePos;
			// move it onwards to skip the skirts we don't need to update
			pRowPosBuf += destPosRowSkip * (skirtStartX - mOffsetX) / skirtSpacing;
			pRowPosBuf += nVertSizePos * (skirtStartY - mOffsetY) / inc;
		}
		if (mVertexDataRecord->cpuVertexDeltaData)
		{
			// Delta dest buffer just after the main vertex data and skirt rows
			pRowDeltaBuf = pRootDeltaBuf + nVertSizeDelta * mVertexDataRecord->size * mVertexDataRecord->size;
			// skip the row skirts
			pRowDeltaBuf += mVertexDataRecord->numSkirtRowsCols * mVertexDataRecord->size * nVertSizeDelta;
			// move it onwards to skip the skirts we don't need to update
			pRowDeltaBuf += destDeltaRowSkip * (skirtStartX - mOffsetX) / skirtSpacing;
			pRowDeltaBuf += nVertSizeDelta * (skirtStartY - mOffsetY) / inc;
		}
		
		for (uint16 x = skirtStartX; x < rect.right; x += skirtSpacing)
		{
			float* pPosBuf = static_cast<float*>(static_cast<void*>(pRowPosBuf));
			float* pDeltaBuf = static_cast<float*>(static_cast<void*>(pRowDeltaBuf));
			for (uint16 y = skirtStartY; y < rect.bottom; y += inc)
			{
				if (mVertexDataRecord->cpuVertexPosData)
				{
					float height = mTerrain->getHeightAtPoint(x, y);
					mTerrain->getPoint(x, y, height, &pos);
					// relative to local centre
					pos -= mLocalCentre;
					pos += skirtOffset;
					
					writePosVertex(x, y, height - mTerrain->getSkirtSize(), pos, uvScale, &pPosBuf);

				}
				if (mVertexDataRecord->cpuVertexDeltaData)
				{
					// delta (none)
					// delta threshold (irrelevant)
					writeDeltaVertex(x, y, 0, 99, &pDeltaBuf);
				}
			}
			if (pRowPosBuf)
				pRowPosBuf += destPosRowSkip;
			if (pRowDeltaBuf)
				pRowDeltaBuf += destDeltaRowSkip;
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::writePosVertex(uint16 x, uint16 y, float height, 
		const VEC3& pos, float uvScale, float** ppPos)
	{
		float* pPosBuf = *ppPos;
		
		short* pPosShort = static_cast<short*>(static_cast<void*>(pPosBuf));
		*pPosShort++ = (short)x;
		*pPosShort++ = (short)y;
		pPosBuf = static_cast<float*>(static_cast<void*>(pPosShort));

		*pPosBuf++ = height;
		
		*ppPos = pPosBuf;
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::writeDeltaVertex(uint16 x, uint16 y, 
		float delta, float deltaThresh, float** ppDelta)
	{
		*(*ppDelta)++ = delta;
		*(*ppDelta)++ = deltaThresh;
	}
	//---------------------------------------------------------------------
	uint16 TerrainQuadTreeNode::calcSkirtVertexIndex(uint16 mainIndex, bool isCol)
	{
		const VertexDataRecord* vdr = getVertexDataRecord();
		// row / col in main vertex resolution
		uint16 row = mainIndex / vdr->size;
		uint16 col = mainIndex % vdr->size;
		
		// skrits are after main vertices, so skip them
		uint16 base = vdr->size * vdr->size;

		// The layout in vertex data is:
		// 1. row skirts
		//    numSkirtRowsCols rows of resolution vertices each
		// 2. column skirts
		//    numSkirtRowsCols cols of resolution vertices each

		// No offsets used here, this is an index into the current vertex data, 
		// which is already relative
		if (isCol)
		{
			uint16 skirtNum = col / vdr->skirtRowColSkip;
			uint16 colbase = vdr->numSkirtRowsCols * vdr->size;
			return base + colbase + vdr->size * skirtNum + row;
		}
		else
		{
			uint16 skirtNum = row / vdr->skirtRowColSkip;
			return base + vdr->size * skirtNum + col;
		}
		
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::destroyCpuVertexData()
	{
		if (mVertexDataRecord)
		{
			// avoid copy empty buffer
			mVertexDataRecord->gpuVertexDataDirty = false;

			if (mVertexDataRecord->cpuVertexPosData)
			{
				delete[]mVertexDataRecord->cpuVertexPosData;
				mVertexDataRecord->cpuVertexPosData = nullptr;
			}

			if (mVertexDataRecord->cpuVertexDeltaData)
			{
				delete[]mVertexDataRecord->cpuVertexDeltaData;
				mVertexDataRecord->cpuVertexDeltaData = nullptr;
			}
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::populateIndexData(uint16 batchSize, IndexBuffer** ppIndexBuf, uint32& nIndexCount)
	{
		const VertexDataRecord* vdr = getVertexDataRecord();

		// Ratio of the main terrain resolution in relation to this vertex data resolution
		size_t resolutionRatio = (mTerrain->getSize() - 1) / (vdr->resolution - 1);
		// At what frequency do we sample the vertex data we're using?
		// mSize is the coverage in terms of the original terrain data (not split to fit in 16-bit)
		size_t vertexIncrement = (mSize-1) / (batchSize-1);
		// however, the vertex data we're referencing may not be at the full resolution anyway
		vertexIncrement /= resolutionRatio;
		uint16 vdatasizeOffsetX = (mOffsetX - mNodeWithVertexData->mOffsetX) / resolutionRatio;
		uint16 vdatasizeOffsetY = (mOffsetY - mNodeWithVertexData->mOffsetY) / resolutionRatio;

		*ppIndexBuf = mTerrain->getGpuBufferAllocator()->getSharedIndexBuffer(batchSize, vdr->size,
			vertexIncrement, vdatasizeOffsetX, vdatasizeOffsetY, 
			vdr->numSkirtRowsCols, vdr->skirtRowColSkip, nIndexCount);

		// shared index buffer is pre-populated		
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::createGpuVertexData()
	{
		// TODO - mutex cpu data
		if (mVertexDataRecord && mVertexDataRecord->cpuVertexPosData && !mVertexDataRecord->gpuPosVertexBuf)
		{
			// get new buffers
			mTerrain->getGpuBufferAllocator()->allocateVertexBuffers(mTerrain, mVertexDataRecord->cpuVertexCount, 
				&mVertexDataRecord->gpuPosVertexBuf, &mVertexDataRecord->gpuDeltaVertexBuf);
				
			// copy data
			mVertexDataRecord->gpuPosVertexBuf->UpdateBuf(mVertexDataRecord->cpuVertexPosData);
			mVertexDataRecord->gpuDeltaVertexBuf->UpdateBuf(mVertexDataRecord->cpuVertexDeltaData);

			// We don't need the CPU copy anymore
			destroyCpuVertexData();
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::updateGpuVertexData()
	{
		if (mVertexDataRecord && mVertexDataRecord->gpuVertexDataDirty)
		{
			mVertexDataRecord->gpuPosVertexBuf->UpdateBuf(mVertexDataRecord->cpuVertexPosData);
			mVertexDataRecord->gpuDeltaVertexBuf->UpdateBuf(mVertexDataRecord->cpuVertexDeltaData);
			mVertexDataRecord->gpuVertexDataDirty = false;
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::destroyGpuVertexData()
	{
		if (mVertexDataRecord)
		{
			// Before we delete, free up the vertex buffers for someone else
			mTerrain->getGpuBufferAllocator()->freeVertexBuffers(mVertexDataRecord->gpuPosVertexBuf, mVertexDataRecord->gpuDeltaVertexBuf);
			delete mVertexDataRecord->gpuPosVertexBuf;
			delete mVertexDataRecord->gpuDeltaVertexBuf;
			mVertexDataRecord->gpuPosVertexBuf = 0;
			mVertexDataRecord->gpuDeltaVertexBuf = 0;
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::createGpuIndexData()
	{
		for (size_t lod = 0; lod < mLodLevels.size(); ++lod)
		{
			LodLevel* ll = mLodLevels[lod];

			if (!ll->pIndexBuf)
			{
				// clone, using default buffer manager ie hardware
				populateIndexData(ll->batchSize, &ll->pIndexBuf, ll->nIndexCount);
			}

			if (ll->nIndexCount == 0)
			{
				int asdf = 0;
			}
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::destroyGpuIndexData()
	{
		for (size_t lod = 0; lod < mLodLevels.size(); ++lod)
		{
			LodLevel* ll = mLodLevels[lod];

			delete ll->pIndexBuf;
			ll->pIndexBuf = 0;
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::mergeIntoBounds(long x, long y, const VEC3& pos)
	{
		if (pointIntersectsNode(x, y))
		{
			VEC3 localPos = pos - mLocalCentre;
			mAABB.Merge(localPos);
			mBoundingRadius = std::max(mBoundingRadius, localPos.GetLength());
			
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
					mChildren[i]->mergeIntoBounds(x, y, pos);
			}
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::resetBounds(const Rect& rect)
	{
		if (rectContainsNode(rect))
		{
			mAABB.SetNull();
			mBoundingRadius = 0;
			
			if (!isLeaf())
			{
				for (int i = 0; i < 4; ++i)
					mChildren[i]->resetBounds(rect);
			}
		}
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::rectContainsNode(const Rect& rect)
	{
		return (rect.left <= mOffsetX && rect.right > mBoundaryX &&
			rect.top <= mOffsetY && rect.bottom > mBoundaryY);
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::rectIntersectsNode(const Rect& rect)
	{
		return (rect.right >= mOffsetX && rect.left <= mBoundaryX &&
				rect.bottom >= mOffsetY && rect.top <= mBoundaryY);
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::pointIntersectsNode(long x, long y)
	{
		return x >= mOffsetX && x < mBoundaryX && 
			y >= mOffsetY && y < mBoundaryY;
	}
	//---------------------------------------------------------------------
	const AABB& TerrainQuadTreeNode::getAABB() const
	{
		return mAABB;
	}
	//---------------------------------------------------------------------
	float TerrainQuadTreeNode::getBoundingRadius() const
	{
		return mBoundingRadius;
	}
	//---------------------------------------------------------------------
	float TerrainQuadTreeNode::getMinHeight() const
	{
		switch (mTerrain->getAlignment())
		{
		case Terrain::ALIGN_X_Y:
		default:
			return mAABB.m_minCorner.z;
		case Terrain::ALIGN_X_Z:
			return mAABB.m_minCorner.y;
		case Terrain::ALIGN_Y_Z:
			return mAABB.m_minCorner.x;
		};
	}
	//---------------------------------------------------------------------
	float TerrainQuadTreeNode::getMaxHeight() const
	{
		switch (mTerrain->getAlignment())
		{
		case Terrain::ALIGN_X_Y:
		default:
			return mAABB.m_maxCorner.z;
		case Terrain::ALIGN_X_Z:
			return mAABB.m_maxCorner.y;
		case Terrain::ALIGN_Y_Z:
			return mAABB.m_maxCorner.x;
		};

	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::calculateCurrentLod(float cFactor)
	{
		mSelfOrChildRendered = false;

		// early-out
		/* disable this, could cause 'jumps' in LOD as children go out of frustum
		if (!cam->isVisible(mMovable->getWorldBoundingBox(true)))
		{
			mCurrentLod = -1;
			return mSelfOrChildRendered;
		}
		*/

		// Check children first
		int childRenderedCount = 0;
		if (!isLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				if (mChildren[i]->calculateCurrentLod(cFactor))
					++childRenderedCount;
			}

		}

		if (childRenderedCount == 0)
		{

			// no children were within their LOD ranges, so we should consider our own
			VEC3 localPos = g_env.pSceneMgr->GetCamera()->GetPos() - mLocalCentre - mTerrain->getPosition();
			float dist;
			if (g_env.pSceneMgr->GetTerrainOptions()->getUseRayBoxDistanceCalculation())
			{
				// Get distance to this terrain node (to closest point of the box)
				// head towards centre of the box (note, box may not cover mLocalCentre because of height)
				//VEC3 dir(mAABB.getCenter() - localPos);
				//dir.normalise();
				//Ray ray(localPos, dir);
				//std::pair<bool, float> intersectRes = Math::intersects(ray, mAABB);

				//// ray will always intersect, we just want the distance
				//dist = intersectRes.second;
				_AST(0);
			}
			else
			{
				// distance to tile centre
				dist = localPos.GetLength();
				// deduct half the radius of the box, assume that on average the 
				// worst case is best approximated by this
				dist -= (mBoundingRadius * 0.5f);
			}

			// For each LOD, the distance at which the LOD will transition *downwards*
			// is given by 
			// distTransition = maxDelta * cFactor;
			uint32 lodLvl = 0;
			mCurrentLod = -1;
			for (LodLevelList::iterator i = mLodLevels.begin(); i != mLodLevels.end(); ++i, ++lodLvl)
			{
				// If we have no parent, and this is the lowest LOD, we always render
				// this is the 'last resort' so to speak, we always enoucnter this last
				if (lodLvl+1 == mLodLevels.size() && !mParent)
				{
					mCurrentLod = lodLvl;
					mSelfOrChildRendered = true;
					mLodTransition = 0;
				}
				else
				{
					// check the distance
					LodLevel* ll = *i;

					// Calculate or reuse transition distance
					float distTransition;
					if (Common::Equal(cFactor, ll->lastCFactor))
						distTransition = ll->lastTransitionDist;
					else
					{
						distTransition = ll->maxHeightDelta * cFactor;
						ll->lastCFactor = cFactor;
						ll->lastTransitionDist = distTransition;
					}

					if (dist < distTransition)
					{
						// we're within range of this LOD
						mCurrentLod = lodLvl;
						mSelfOrChildRendered = true;

						if (mTerrain->_getMorphRequired())
						{
							// calculate the transition percentage
							// we need a percentage of the total distance for just this LOD, 
							// which means taking off the distance for the next higher LOD
							// which is either the previous entry in the LOD list, 
							// or the largest of any children. In both cases these will
							// have been calculated before this point, since we process
							// children first. Distances at lower LODs are guaranteed
							// to be larger than those at higher LODs

							float distTotal = distTransition;
							if (isLeaf())
							{
								// Any higher LODs?
								if (i != mLodLevels.begin())
								{
									LodLevelList::iterator prev = i - 1;
									distTotal -= (*prev)->lastTransitionDist;
								}
							}
							else
							{
								// Take the distance of the lowest LOD of child
								const LodLevel* childLod = mChildWithMaxHeightDelta->getLodLevel(
									mChildWithMaxHeightDelta->getLodCount()-1);
								distTotal -= childLod->lastTransitionDist;
							}
							// fade from 0 to 1 in the last 25% of the distance
							float distMorphRegion = distTotal * 0.25f;
							float distRemain = distTransition - dist;

							mLodTransition = 1.0f - (distRemain / distMorphRegion);
							mLodTransition = std::min(1.0f, mLodTransition);
							mLodTransition = std::max(0.0f, mLodTransition);

							// Pass both the transition % and target LOD (GLOBAL current + 1)
							// this selectively applies the morph just to the
							// vertices which would drop out at this LOD, even 
							// while using the single shared vertex data
							mTerrain->m_cbTerrain.lodMorph = VEC2(mLodTransition, mCurrentLod + mBaseLod + 1);
						}
						// since LODs are ordered from highest to lowest detail, 
						// we can stop looking now
						break;
					}

				}
			}

		}
		else 
		{
			// we should not render ourself
			mCurrentLod = -1;
			mSelfOrChildRendered = true; 
			if (childRenderedCount < 4)
			{
				// only *some* children decided to render on their own, but either 
				// none or all need to render, so set the others manually to their lowest
				for (int i = 0; i < 4; ++i)
				{
					TerrainQuadTreeNode* child = mChildren[i];
					if (!child->isSelfOrChildRenderedAtCurrentLod())
					{
						child->setCurrentLod(child->getLodCount()-1);
						child->setLodTransition(1.0);
					}
				}
			} // (childRenderedCount < 4)

		} // (childRenderedCount == 0)


		return mSelfOrChildRendered;

	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::setCurrentLod(int lod)
	{
		 mCurrentLod = lod;
		 mTerrain->m_cbTerrain.lodMorph = VEC2(mLodTransition, mCurrentLod + mBaseLod + 1);
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::setLodTransition(float t)
	{
		mLodTransition = t; 	
		mTerrain->m_cbTerrain.lodMorph = VEC2(mLodTransition, mCurrentLod + mBaseLod + 1);
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::isRenderedAtCurrentLod() const
	{
		return mCurrentLod != -1;
	}
	//---------------------------------------------------------------------
	bool TerrainQuadTreeNode::isSelfOrChildRenderedAtCurrentLod() const
	{
		return mSelfOrChildRendered;
	}
	//------------------------------------------------------------------------------------
	void TerrainQuadTreeNode::CreateEntity()
	{
		// Create the entity.
		if (isRenderedAtCurrentLod() && !mEntity)
		{
			Mesh* pMesh = new Mesh;
			SubMesh* pSubmesh = new SubMesh;

			pMesh->SetPrimitiveType(ePrimitive_TriangleStrip);
			pSubmesh->InitTerrainVertData(getVertexDataRecord()->gpuPosVertexBuf, getVertexDataRecord()->gpuDeltaVertexBuf,
				mLodLevels[mCurrentLod]->pIndexBuf, mLodLevels[mCurrentLod]->nIndexCount);

			pMesh->AddSubMesh(pSubmesh);
			mEntity = new Entity(pMesh);

			// vertex data is generated in terrain space
			mEntity->SetPosition(mTerrain->getPosition());

			static uint32 iMat = 0;
			++iMat;
			char szMatName[64];
			sprintf_s(szMatName, 64, "Mtl_TerrainSector_%d", iMat);

			Material* pMaterial = MaterialManager::GetSingleton().NewMaterial(szMatName, eVertexType_Terrain);

			pMaterial->SetTexture(0, mTerrain->getTerrainNormalMap());
			pMaterial->SetTexture(1, mTerrain->getLayerBlendTexture(0));

			SSamplerDesc& samDesc = pMaterial->GetSamplerStateDesc(0);
			samDesc.AddressU = eTextureAddressMode_WRAP;
			samDesc.AddressV = eTextureAddressMode_WRAP;
			samDesc.Filter = SF_MIN_MAG_MIP_LINEAR;
			pMaterial->SetSamplerStateDesc(0, samDesc);
			pMaterial->SetSamplerStateDesc(1, samDesc);

			for (uint32 i = 0; i < mTerrain->getLayerCount(); ++i)
			{
				pMaterial->SetTexture(2+i*2, g_env.pRenderer->GetRenderSys()->LoadTexture(mTerrain->getLayerTextureName(i, 0), eTextureType_2D, 0, true));
				pMaterial->SetTexture(2+i*2+1, g_env.pRenderer->GetRenderSys()->LoadTexture(mTerrain->getLayerTextureName(i, 1)));

				pMaterial->SetSamplerStateDesc(2 + i * 2, samDesc);
				pMaterial->SetSamplerStateDesc(2 + i * 2 + 1, samDesc);
			}

			pMaterial->InitShader("Terrain", eShader_Terrain, 0, nullptr, "VS_GBuffer", "PS_GBuffer");
			mEntity->SetMaterial(pMaterial);
		}

		if (mChildren[0])
		{
			mChildren[0]->CreateEntity();
			mChildren[1]->CreateEntity();
			mChildren[2]->CreateEntity();
			mChildren[3]->CreateEntity();
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::Render()
	{
		if (isRenderedAtCurrentLod())
		{
			mEntity->Render();
		}

		if (mChildren[0])
		{
			mChildren[0]->Render();
			mChildren[1]->Render();
			mChildren[2]->Render();
			mChildren[3]->Render();
		}
	}
	//---------------------------------------------------------------------
	void TerrainQuadTreeNode::enableWireframe(bool b)
	{
		if (mEntity)
		{
			mEntity->GetMaterial()->SetFillMode(b ? eFill_Wireframe : eFill_Solid);
		}

		if (mChildren[0])
		{
			mChildren[0]->enableWireframe(b);
			mChildren[1]->enableWireframe(b);
			mChildren[2]->enableWireframe(b);
			mChildren[3]->enableWireframe(b);
		}
	}
	//---------------------------------------------------------------------
	//float TerrainQuadTreeNode::getSquaredViewDepth(const Camera* cam) const
	//{
	//	return mMovable->getParentSceneNode()->getSquaredViewDepth(cam);
	//}
}

