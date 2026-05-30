#include "DOFPass.h"
#include "RenderPassRegistry.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Command/DrawCommandList.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Resource/RenderResources.h"

REGISTER_RENDER_PASS(FDOFPass)

namespace 
{
	// Shared constant buffer for all DOF passes (CoC / Blur / Composite).
	struct FDOFCB
	{
		float Aperture;
		float FocusDist;
		float FocalLength;
		int   DOFSample;
		float CameraNear;
		float CameraFar;
		float SourceTexelSize[2];
	};

	void UnBindSRVSingle(ID3D11DeviceContext* DC)
	{
		ID3D11ShaderResourceView* NullSRV = nullptr;
		DC->VSSetShaderResources(0, 1, &NullSRV);
		DC->PSSetShaderResources(0, 1, &NullSRV);
	}

	void UnBindSRVDouble(ID3D11DeviceContext* DC)
	{
		ID3D11ShaderResourceView* NullSRVs[2] = {};
		DC->VSSetShaderResources(0, 2, NullSRVs);
		DC->PSSetShaderResources(0, 2, NullSRVs);
	}

	void UnBindSRVTriple(ID3D11DeviceContext* DC)
	{
		ID3D11ShaderResourceView* NullSRVs[3] = {};
		DC->VSSetShaderResources(0, 3, NullSRVs);
		DC->PSSetShaderResources(0, 3, NullSRVs);
	}

	// Pass to shared hlsli?
	void BindDOFCB(ID3D11DeviceContext* DC, FConstantBuffer& Buffer)
	{
		ID3D11Buffer* RawCB = Buffer.GetBuffer();
		DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &RawCB);
		DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &RawCB);
	}

	void DrawFullscreen(ID3D11DeviceContext* DC, FShader* Shader)
	{
		Shader->Bind(DC);
		DC->Draw(3, 0);
	}

} // anonymous namespace

FDOFPass::FDOFPass()
{
	PassType = ERenderPass::DepthOfField;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
					ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}

bool FDOFPass::BeginPass(const FPassContext& Ctx) 
{
	const FFrameContext& FrameContext = Ctx.Frame;
	const FViewportRenderOptions& RenderOptions = FrameContext.RenderOptions;
	if (RenderOptions.ShowFlags.bDOF == false || RenderOptions.Aperture <= 0.001f)		return false;
	if (!FrameContext.SceneColorCopyTexture || !FrameContext.SceneColorCopySRV ||
		!FrameContext.ViewportRenderTexture || !FrameContext.ViewportRTV)			return false;
	if (FrameContext.ViewportWidth <= 0.0f || FrameContext.ViewportHeight <= 0.0f)	return false;

	// Ensure Constant Buffer
	if (!DOFCB.GetBuffer())
	{
		DOFCB.Create(Ctx.Device.GetDevice(), sizeof(FDOFCB), "DOFCB");
		if (!DOFCB.GetBuffer()) return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();

	// Safeguard
	UnBindSRVDouble(DC);
	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(FrameContext.SceneColorCopyTexture, FrameContext.ViewportRenderTexture);
	DC->CopyResource(FrameContext.DepthCopyTexture, FrameContext.DepthTexture);

	Ctx.Resources.SetDepthStencilState(Ctx.Device, EDepthStencilState::NoDepth);
	Ctx.Resources.SetBlendState(Ctx.Device, EBlendState::Opaque);
	Ctx.Resources.SetRasterizerState(Ctx.Device, ERasterizerState::SolidNoCull);

	ID3D11Buffer* NullVB = nullptr;
	uint32 Zero = 0;
	DC->IASetVertexBuffers(0, 1, &NullVB, &Zero, &Zero);
	DC->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
	DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	Ctx.Cache.bForceAll = true;
	return true;
}

void FDOFPass::Execute(const FPassContext& Ctx)
{
	FShader* CoCShader = FShaderManager::Get().GetOrCreate(EShaderPath::DOFCoC);
	FShader* BlurShader = FShaderManager::Get().GetOrCreate(EShaderPath::DOFBlur);
	FShader* CompositeShader = FShaderManager::Get().GetOrCreate(EShaderPath::DOFComposite);

	if (!CoCShader || !CoCShader->IsValid() ||
		!BlurShader || !BlurShader->IsValid() ||
		!CompositeShader || !CompositeShader->IsValid()) return;

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	const FFrameContext& FrameContext = Ctx.Frame;
	const FViewportRenderOptions& RenderOptions = FrameContext.RenderOptions;

	FDOFCB DOFCBData = {};
	DOFCBData.Aperture	= RenderOptions.Aperture;
	DOFCBData.FocusDist = RenderOptions.FocusDistance;
	DOFCBData.FocalLength = RenderOptions.FocalLength;
	DOFCBData.DOFSample = RenderOptions.DOFSample;
	DOFCBData.CameraNear = FrameContext.NearClip;
	DOFCBData.CameraFar = FrameContext.FarClip;
	DOFCBData.SourceTexelSize[0] = 1.0f / FrameContext.ViewportWidth;
	DOFCBData.SourceTexelSize[1] = 1.0f / FrameContext.ViewportHeight;

	// CoC Pass
	DC->OMSetRenderTargets(1, &FrameContext.DOFResources->CoCResources.RTV, nullptr);
	DOFCB.Update(DC, &DOFCBData, sizeof(DOFCBData));
	BindDOFCB(DC, DOFCB);
	ID3D11ShaderResourceView* CoCSRVs[2] = { FrameContext.SceneColorCopySRV, FrameContext.DepthCopySRV };
	DC->PSSetShaderResources(0, 2, CoCSRVs);
	DrawFullscreen(DC, CoCShader);
	UnBindSRVDouble(DC);

	// Blur Pass
	DC->OMSetRenderTargets(1, &FrameContext.DOFResources->BlurResources.RTV, nullptr);
	DC->PSSetShaderResources(0, 1, &FrameContext.DOFResources->CoCResources.SRV);
	DrawFullscreen(DC, BlurShader);
	UnBindSRVSingle(DC);

	// Composite Pass
	DC->OMSetRenderTargets(1, &FrameContext.ViewportRTV, nullptr); 
	ID3D11ShaderResourceView* CompositeSRVs[2] = {
		FrameContext.SceneColorCopySRV,                  // sharp
		FrameContext.DOFResources->BlurResources.SRV,    // blurred
	};
	DC->PSSetShaderResources(0, 2, CompositeSRVs);
	DrawFullscreen(DC, CompositeShader);
	UnBindSRVDouble(DC);
}

void FDOFPass::EndPass(const FPassContext& Ctx)
{
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();

	// Restore the main render target
	ID3D11RenderTargetView* ViewportRTV = Ctx.Frame.ViewportRTV;
	DC->OMSetRenderTargets(1, &ViewportRTV, Ctx.Frame.ViewportDSV);
	Ctx.Cache.bForceAll = true;
}