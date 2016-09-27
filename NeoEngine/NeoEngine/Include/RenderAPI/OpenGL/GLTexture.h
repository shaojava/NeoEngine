/********************************************************************
	created:	2016/09/12 15:52
	filename	GLTexture.h
	author:		maval

	purpose:	GL texture related.
*********************************************************************/
#ifndef GLTexture_h__
#define GLTexture_h__


#include "Prerequiestity.h"
#include "Texture.h"
#include "RenderState.h"

namespace Neo
{
	//------------------------------------------------------------------------------------
	class GLTexture : public Texture
	{
	public:
		// Load from file
		GLTexture(const STRING& filename, eTextureType type, uint32 usage, bool bSRGB);
		// Create as manual
		GLTexture(uint32 width, uint32 height, const void* pTexData, ePixelFormat format, uint32 usage, bool bMipMap);

		~GLTexture();

	public:
		virtual void*		GetDSV() const { _AST(0); return nullptr; }
		virtual void*		GetSRV() const { return (void*)m_id; }
		virtual void*		GetRTV() const { return (void*)m_id; }
		virtual void*		GetRTV(uint32 iSlice) const { _AST(0); return nullptr; }
		virtual void		GenMipMaps();
		virtual void		Resize(uint32 nWidth, uint32 nHeight) { _AST(0); }

	private:
		void				_Init(uint32 width, uint32 height, const void* pTexData, ePixelFormat format, uint32 usage, uint32 nMips);

		GLuint				m_id;
		bool				m_bsRGB;
	};

	//------------------------------------------------------------------------------------
	class GLSamplerState : public SamplerState
	{
	public:
		GLSamplerState(const SSamplerDesc& desc);
		~GLSamplerState();

		virtual	void	Apply(uint32 iStage, bool bVS, bool bGS, bool bTessellation);

		GLuint			m_id;
	};
}

#endif // GLTexture_h__


