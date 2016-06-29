#include "stdafx.h"
#include "TiledRenderer.h"
#include "Material.h"
#include "StructuredBuffer.h"
#include "D3D11RenderSystem.h"
#include "SceneManager.h"
#include "D3D11RenderTarget.h"
#include "D3D11Texture.h"

namespace Neo
{
	const uint32	COMPUTE_SHADER_TILE_GROUP_DIM = 16;
	//------------------------------------------------------------------------------------
	TileBasedDeferredRenderer::TileBasedDeferredRenderer()
	{
		m_pDeviceContext = g_env.pRenderSystem->GetDeviceContext();

		m_pMtlTiledCS = new Material;
		m_pMtlTiledCS->InitComputeShader(GetResPath("TiledCS.hlsl"));

		ID3D11Device* pDevice = g_env.pRenderSystem->GetDevice();
		const uint32 nBufferSize = g_env.pRenderSystem->GetWndWidth() * g_env.pRenderSystem->GetWndHeight();
		m_pLitBuffer = new StructuredBuffer<FramebufferFlatElement>(pDevice, nBufferSize, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);

		m_pLightParamBuffer = new StructuredBuffer<SPointLightCS>(pDevice, MAX_POINT_LIGHT, D3D11_BIND_SHADER_RESOURCE, true);

		D3D11_SAMPLER_DESC samDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
		samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		g_env.pRenderSystem->GetDevice()->CreateSamplerState(&samDesc, &m_pSamp);
	}
	//------------------------------------------------------------------------------------
	TileBasedDeferredRenderer::~TileBasedDeferredRenderer()
	{
		SAFE_RELEASE(m_pSamp);
		SAFE_RELEASE(m_pMtlTiledCS);
		SAFE_RELEASE(m_pLitBuffer);
		SAFE_RELEASE(m_pLightParamBuffer);
	}
	//------------------------------------------------------------------------------------
	void TileBasedDeferredRenderer::Render()
	{
		ID3D11ShaderResourceView* pLightParamSRV = _SetupLight();

		g_env.pRenderSystem->SetRenderTarget(nullptr, nullptr, 1, false, false);

		ID3D11ShaderResourceView* pSRV1 = g_env.pSceneMgr->GetAlbedoRT()->GetRenderTexture()->GetSRV();
		ID3D11ShaderResourceView* pSRV2 = g_env.pSceneMgr->GetNormalRT()->GetRenderTexture()->GetSRV();
		ID3D11ShaderResourceView* pSRV3 = g_env.pSceneMgr->GetSpecRT()->GetRenderTexture()->GetSRV();
		ID3D11ShaderResourceView* pSRV4 = g_env.pSceneMgr->GetDepthRT()->GetRenderTexture()->GetSRV();

		m_pDeviceContext->CSSetShaderResources(0, 1, &pSRV1);
		m_pDeviceContext->CSSetShaderResources(1, 1, &pSRV2);
		m_pDeviceContext->CSSetShaderResources(2, 1, &pSRV3);
		m_pDeviceContext->CSSetShaderResources(3, 1, &pSRV4);
		m_pDeviceContext->CSSetShaderResources(4, 1, &pLightParamSRV);

		auto pUAV = m_pLitBuffer->GetUnorderedAccess();
		m_pDeviceContext->CSSetUnorderedAccessViews(0, 1, &pUAV, 0);

		m_pMtlTiledCS->Activate();

		m_pDeviceContext->CSSetSamplers(0, 1, &m_pSamp);

		// Dispatch
		g_env.pRenderSystem->UpdateGlobalCBuffer(false, true);

		unsigned int dispatchWidth = (g_env.pRenderSystem->GetWndWidth() + COMPUTE_SHADER_TILE_GROUP_DIM - 1) / COMPUTE_SHADER_TILE_GROUP_DIM;
		unsigned int dispatchHeight = (g_env.pRenderSystem->GetWndHeight() + COMPUTE_SHADER_TILE_GROUP_DIM - 1) / COMPUTE_SHADER_TILE_GROUP_DIM;
		m_pDeviceContext->Dispatch(dispatchWidth, dispatchHeight, 1);

		ID3D11ShaderResourceView* pNullSRV[5] = {nullptr};
		ID3D11UnorderedAccessView* pNullUAV = nullptr;

		m_pMtlTiledCS->TurnOffComputeShader();
		m_pDeviceContext->CSSetShaderResources(0, 5, pNullSRV);
		m_pDeviceContext->CSSetUnorderedAccessViews(0, 1, &pNullUAV, 0);
	}
	//------------------------------------------------------------------------------------
	ID3D11ShaderResourceView* TileBasedDeferredRenderer::_SetupLight()
	{
		const PointLightVector& vecLights = g_env.pSceneMgr->GetPointLights();
		
		// Copy light list into shader buffer
		SPointLightCS* pData = m_pLightParamBuffer->MapDiscard(m_pDeviceContext);

		for (uint32 i = 0; i < vecLights.size(); ++i)
		{
			const SPointLight& light = vecLights[i];

			pData[i].position = light.vPos;
			pData[i].attenuationBegin = light.fAtten;
			pData[i].attenuationEnd = light.fRadius;
			pData[i].color.Set(light.color.r, light.color.g, light.color.b);
		}
		
		m_pLightParamBuffer->Unmap(m_pDeviceContext);

		return m_pLightParamBuffer->GetShaderResource();
	}

}

