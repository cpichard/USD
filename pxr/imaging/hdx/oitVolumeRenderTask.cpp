//
// Copyright 2019 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdx/oitVolumeRenderTask.h"
#include "pxr/imaging/hdx/package.h"
#include "pxr/imaging/hdx/oitBufferAccessor.h"

#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/rprimCollection.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include "pxr/imaging/hdSt/renderPassShader.h"
#include "pxr/imaging/hdSt/tokens.h"
#include "pxr/imaging/hdSt/volume.h"

#include "pxr/imaging/glf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

static const HioGlslfxSharedPtr &
_GetRenderPassOitVolumeGlslfx()
{
    static const HioGlslfxSharedPtr glslfx =
        std::make_shared<HioGlslfx>(HdxPackageRenderPassOitVolumeShader());
    return glslfx;
}

HdxOitVolumeRenderTask::HdxOitVolumeRenderTask(
                HdSceneDelegate* delegate, SdfPath const& id)
    : HdxRenderTask(delegate, id)
    , _oitVolumeRenderPassShader(
        std::make_shared<HdStRenderPassShader>(
            _GetRenderPassOitVolumeGlslfx()))
    , _isOitEnabled(HdxOitBufferAccessor::IsOitEnabled())
{
}

HdxOitVolumeRenderTask::~HdxOitVolumeRenderTask() = default;

void
HdxOitVolumeRenderTask::_Sync(
    HdSceneDelegate* delegate,
    HdTaskContext* ctx,
    HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_isOitEnabled) {
        HdxRenderTask::_Sync(delegate, ctx, dirtyBits);
    }
}

void
HdxOitVolumeRenderTask::Prepare(HdTaskContext* ctx,
                                HdRenderIndex* renderIndex)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // OIT buffers take up significant GPU resources. Skip if there are no
    // oit draw items (i.e. no volumetric draw items)
    if (!_isOitEnabled || !HdxRenderTask::_HasDrawItems()) {
        return;
    }
    
    HdxRenderTask::Prepare(ctx, renderIndex);
    HdxOitBufferAccessor(ctx).RequestOitBuffers();

    if (HdRenderPassStateSharedPtr const state = _GetRenderPassState(ctx)) {
        _oitVolumeRenderPassShader->UpdateAovInputTextures(
            state->GetAovInputBindings(),
            renderIndex);
    }
}

void
HdxOitVolumeRenderTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    GLF_GROUP_FUNCTION();

    if (!_isOitEnabled || !HdxRenderTask::_HasDrawItems()) {
        return;
    }
    
    //
    // Pre Execute Setup
    //

    HdxOitBufferAccessor oitBufferAccessor(ctx);

    oitBufferAccessor.RequestOitBuffers();
    oitBufferAccessor.InitializeOitBuffersIfNecessary(_GetHgi());

    HdRenderPassStateSharedPtr renderPassState = _GetRenderPassState(ctx);
    if (!TF_VERIFY(renderPassState)) return;

    HdStRenderPassState* extendedState =
        dynamic_cast<HdStRenderPassState*>(renderPassState.get());
    if (!TF_VERIFY(extendedState, "OIT only works with HdSt")) {
        return;
    }

    extendedState->SetUseSceneMaterials(true);
    renderPassState->SetEnableDepthTest(false);
    // Setting cull style for consistency even though it is hard-coded in
    // shaders/volume.glslfx.
    renderPassState->SetCullStyle(HdCullStyleBack);

    if(!oitBufferAccessor.AddOitBufferBindings(_oitVolumeRenderPassShader)) {
        TF_CODING_ERROR(
            "No OIT buffers allocated but needed by OIT volume render task");
        return;
    }

    // We render into an SSBO -- not MSSA compatible
    renderPassState->SetMultiSampleEnabled(false);

    // XXX
    //
    // To show volumes that intersect the far clipping plane, we might consider
    // calling glEnable(GL_DEPTH_CLAMP) here.

    //
    // Translucent pixels pass
    //
    extendedState->SetRenderPassShader(_oitVolumeRenderPassShader);
    renderPassState->SetEnableDepthMask(false);
    renderPassState->SetColorMasks({HdRenderPassState::ColorMaskNone});

    // Transition depth texture (if it exists) to shader read layout.
    HgiTextureUsage oldLayout {};
    HgiTextureHandle depthTexture {};
    if (_HasTaskContextData(ctx, HdAovTokens->depth)) {
        _GetTaskContextData(ctx, HdAovTokens->depth, &depthTexture);
        oldLayout =
            depthTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    }
    
    HdxRenderTask::Execute(ctx);

    // Restore old layout.
    if (oldLayout) {
        depthTexture->SubmitLayoutChange(oldLayout);
    }
}


PXR_NAMESPACE_CLOSE_SCOPE
