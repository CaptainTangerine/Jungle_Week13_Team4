#include "PostDOFTransPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FEditorIconPass)

FEditorIconPass::FEditorIconPass()
{
	PassType = ERenderPass::PostDOFTranscluency;
	RenderState = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend,
					ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}
