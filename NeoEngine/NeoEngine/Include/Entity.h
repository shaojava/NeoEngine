/********************************************************************
	created:	6:6:2014   12:10
	filename	Entity.h
	author:		maval

	purpose:	Entity instance in scene graph.
*********************************************************************/
#ifndef Entity_h__
#define Entity_h__

#include "Prerequiestity.h"
#include "MathDef.h"
#include "AABB.h"

namespace Neo
{
	struct FurCustomRenderData 
	{
		FurCustomRenderData()
		{
			bRenderFins = true;
			bRenderShells = true;
			fFinMaxOpacity = 0.6f;
			shellTexTiling = 10;
			shellIncrement = 0.3f;
		}

		float	fFinMaxOpacity;
		float	shellIncrement;
		float	shellTexTiling;
		bool	bRenderFins;
		bool	bRenderShells;
	};
	//------------------------------------------------------------------------------------
	class Entity
	{
	public:
		Entity(Mesh* pMesh, bool bUpdateAABB = true);
		virtual ~Entity();

	public:
		virtual void	Update(float fDeltaTime);
		virtual void	Render();
		virtual void	DebugRender() {}

		void			RenderInstanced(const SInstanedBatch& ib);
		void			SetMaterial(Material* pMaterial);
		Material*		GetMaterial();
		void			SetPosition(const VEC3& pos);
		void			SetRotation(const QUATERNION& quat);
		void			SetScale(float scale);
		void			SetScale(float x, float y, float z);
		void			SetCustomRenderData(void* pData) { m_pCustomRenderData = pData; }
		void*			GetCustomRenderData() { return m_pCustomRenderData; }
		void			UpdateAABB();

		const VEC3&		GetPosition() { return m_position; }
		const QUATERNION&	GetRotation() { return m_rotation; }
		const VEC3&		GetScale() { return m_scale; }

		void			SetWorldMatrix(const MAT44& mat);
		const MAT44&	GetWorldMatrix();
		const MAT44&	GetWorldITMatrix();

		void			SetUpdateAABB(bool b)	{ m_bUpdateAABB = b; }
		void			SetLocalAABB(const AABB& aabb) { m_localAABB = aabb; }
		const AABB&		GetWorldAABB() const	{ return m_worldAABB; }
		Mesh*			GetMesh() { return m_pMesh; }

		void			SetCastShadow(bool bCast) { m_bCastShadow = bCast; }
		bool			GetCastShadow() const	{ return m_bCastShadow; }
		void			SetReceiveShadow(bool bReceive) { m_bReceiveShadow = bReceive; }
		bool			GetReceiveShadow() const	{ return m_bReceiveShadow; }
		void			SetVisible(bool b) { m_bVisible = b; }
		bool			GetVisible() const { return m_bVisible; }

	protected:
		void			_UpdateTransform();
		void			_ComputeAABB();

	protected:
		Mesh*			m_pMesh;
		Material*		m_pMaterial;
		void*			m_pCustomRenderData;

		VEC3			m_position;
		QUATERNION		m_rotation;
		VEC3			m_scale;

		bool			m_bMatrixInvalid;
		MAT44			m_matWorld;
		MAT44			m_matWorldIT;		//世界矩阵的逆转置,用于法线变换

		bool			m_bUpdateAABB;
		AABB			m_localAABB;		//本地包围盒
		AABB			m_worldAABB;		//世界包围盒
		bool			m_bCastShadow;		// Is shadow caster?
		bool			m_bReceiveShadow;	// Is shadow receiver?
		bool			m_bVisible;
	};
}


#endif // Entity_h__