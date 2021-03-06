#include "stdafx.h"
#include "PixelBox.h"

namespace Neo
{
	PixelBox::PixelBox( int width, int height, int bytesPerPixel, void* pData, bool bOwnData)
		: m_width(width)
		, m_height(height)
		, m_bytesPerPixel(bytesPerPixel)
		, m_pitch(width * bytesPerPixel)
		, m_ownData(bOwnData)

	{
		if (pData)
		{
			if (bOwnData)
			{
				memcpy(m_data, pData, m_pitch * height);
			} 
			else
			{
				m_data = (char*)pData;
			}
		} 
		else
		{
			m_data = new char[width * height * bytesPerPixel];
		}
	}

	PixelBox::PixelBox( BITMAP* bm, bool bCopyData )
	{
		m_width = bm->bmWidth;
		m_height = bm->bmHeight;
		m_bytesPerPixel = bm->bmBitsPixel / 8;
		m_pitch = bm->bmWidthBytes;

		if(bCopyData)
		{
			m_data = new char[m_width * m_height * m_bytesPerPixel];
			memcpy(&m_data[0], bm->bmBits, m_width * m_height * m_bytesPerPixel);
			m_ownData = true;
		}
		else
		{
			m_data = (char*)bm->bmBits;
			m_ownData = false;
		}
	}

	PixelBox::~PixelBox()
	{
		if(m_ownData)
			SAFE_DELETE_ARRAY(m_data);
	}

	Neo::SColor PixelBox::GetPixelAt( int x, int y ) const
	{
		assert(x >=0 && x < m_width);
		assert(y >=0 && x < m_height);

		SColor result;
		switch (m_bytesPerPixel)
		{
		case 3:
			{
				char* pData = m_data + y * m_pitch + x * 3;
				result.b = (float)pData[0];
				result.g = (float)pData[1];
				result.r = (float)pData[2];
			}
			break;

		case 4:
			{
				DWORD* pData = (DWORD*)m_data;
				DWORD color = pData[y*m_width+x];
				result.SetAsInt(color);
			}
			break;

		default: assert(0); break;
		}

		return std::move(result);
	}

	void PixelBox::SetPixelAt( int x, int y, SColor p )
	{
		assert(x >=0 && x < m_width);
		assert(y >=0 && x < m_height);

		switch (m_bytesPerPixel)
		{
		case 3:
			{
				char* pData = m_data + y * m_pitch + x * 3;
				pData[0] = char(p.b);
				pData[1] = char(p.g);
				pData[2] = char(p.r);
			}
			break;

		case 4:
			{
				DWORD* pData = (DWORD*)m_data;
				pData[y*m_width+x] = p.GetAsInt();
			}
			break;

		default: assert(0); break;
		}
	}
}

