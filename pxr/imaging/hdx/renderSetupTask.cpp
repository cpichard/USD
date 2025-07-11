//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdx/renderSetupTask.h"
#include "pxr/imaging/hdx/package.h"
#include "pxr/imaging/hdx/tokens.h"
#include "pxr/imaging/hdx/debugCodes.h"

#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/renderBuffer.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hdSt/renderPassShader.h"
#include "pxr/imaging/hdSt/renderPassState.h"
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hdSt/tokens.h"
#include "pxr/imaging/hdSt/volume.h"

#include "pxr/imaging/cameraUtil/conformWindow.h"

PXR_NAMESPACE_OPEN_SCOPE

static const HioGlslfxSharedPtr &
_GetRenderPassColorGlslfx()
{
    static const HioGlslfxSharedPtr glslfx =
        std::make_shared<HioGlslfx>(HdxPackageRenderPassColorShader());
    return glslfx;
}

HdxRenderSetupTask::HdxRenderSetupTask(
    HdSceneDelegate* delegate, SdfPath const& id)
    : HdTask(id)
    , _colorRenderPassShader(
        std::make_shared<HdStRenderPassShader>(_GetRenderPassColorGlslfx()))
    , _viewport(0)
{
}

HdxRenderSetupTask::~HdxRenderSetupTask() = default;

void
HdxRenderSetupTask::Sync(HdSceneDelegate* delegate,
                         HdTaskContext* ctx,
                         HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
        HdxRenderTaskParams params;

        if (!_GetTaskParams(delegate, &params)) {
            return;
        }

        SyncParams(delegate, params);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
HdxRenderSetupTask::Prepare(HdTaskContext* ctx,
                            HdRenderIndex* renderIndex)
{
    _PrepareAovBindings(ctx, renderIndex);
    PrepareCamera(renderIndex);

    HdRenderPassStateSharedPtr &renderPassState =
        _GetRenderPassState(renderIndex);

    const float stepSize = renderIndex->GetRenderDelegate()->
        GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSize,
            HdStVolume::defaultStepSize);
    const float stepSizeLighting = renderIndex->GetRenderDelegate()->
        GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSizeLighting,
            HdStVolume::defaultStepSizeLighting);
    renderPassState->SetVolumeRenderingConstants(stepSize, stepSizeLighting);

    const bool enableExposureCompensation = renderIndex->GetRenderDelegate()->
        GetRenderSetting<bool>(
            HdRenderSettingsTokens->enableExposureCompensation, true);
    renderPassState->SetEnableExposureCompensation(enableExposureCompensation);

    if (HdStRenderPassState * const hdStRenderPassState =
            dynamic_cast<HdStRenderPassState*>(renderPassState.get())) {
        _SetRenderpassShadersForStorm(hdStRenderPassState,
            renderIndex->GetResourceRegistry());
    }

    renderPassState->Prepare(renderIndex->GetResourceRegistry());
    (*ctx)[HdxTokens->renderPassState] = VtValue(_renderPassState);
}

void
HdxRenderSetupTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // set raster state to TaskContext
    (*ctx)[HdxTokens->renderPassState] = VtValue(_renderPassState);
}

static
bool
_AovHasIdSemantic(HdRenderPassAovBinding const &binding)
{
    // For now id render only means primId or instanceId.
    return binding.aovName == HdAovTokens->primId ||
        binding.aovName == HdAovTokens->instanceId;
}

static
bool
_AovHasColorSemantic(HdRenderPassAovBinding const &binding)
{
    return binding.aovName == HdAovTokens->color;
}

void
HdxRenderSetupTask::_SetRenderpassShadersForStorm(
    HdStRenderPassState *renderPassState,
    HdResourceRegistrySharedPtr const &resourceRegistry)
{
    // If no aovs are bound, use pre-assembled color render pass shader.
    if (_aovBindings.empty()) {
        renderPassState->SetRenderPassShader(_colorRenderPassShader);
        return;
    }

    HdStResourceRegistrySharedPtr const& hdStResourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(resourceRegistry);

    renderPassState->SetRenderPassShader(
        HdStRenderPassShader::CreateRenderPassShaderFromAovs(
            renderPassState,
            hdStResourceRegistry,
            _aovBindings));
}

void
HdxRenderSetupTask::SyncParams(HdSceneDelegate* delegate,
                               HdxRenderTaskParams const &params)
{
    _viewport = params.viewport;
    _framing = params.framing;
    _overrideWindowPolicy = params.overrideWindowPolicy;
    _cameraId = params.camera;
    _aovBindings = params.aovBindings;
    _aovInputBindings = params.aovInputBindings;

    // Inspect AOVs to see if we're doing color or id rendering.
    const bool usingIdAov = std::find_if(_aovBindings.begin(),
        _aovBindings.end(), &_AovHasIdSemantic) !=
        _aovBindings.end();
    const bool usingColorAov = std::find_if(_aovBindings.begin(),
        _aovBindings.end(), &_AovHasColorSemantic) !=
        _aovBindings.end();
    const bool usingNoAovs = _aovBindings.empty();

    HdRenderIndex &renderIndex = delegate->GetRenderIndex();
    HdRenderPassStateSharedPtr &renderPassState =
            _GetRenderPassState(&renderIndex);

    renderPassState->SetOverrideColor(params.overrideColor);
    renderPassState->SetWireframeColor(params.wireframeColor);
    renderPassState->SetPointColor(params.pointColor);
    renderPassState->SetPointSize(params.pointSize);
    renderPassState->SetLightingEnabled(params.enableLighting);
    renderPassState->SetClippingEnabled(params.enableClipping);
    renderPassState->SetAlphaThreshold(params.alphaThreshold);
    renderPassState->SetCullStyle(params.cullStyle);

    renderPassState->SetMaskColor(params.maskColor);
    renderPassState->SetIndicatorColor(params.indicatorColor);
    renderPassState->SetPointSelectedSize(params.pointSelectedSize);

    // Storm render pipeline state
    {
        renderPassState->SetDepthBiasUseDefault(params.depthBiasUseDefault);
        renderPassState->SetDepthBiasEnabled(params.depthBiasEnable);
        renderPassState->SetDepthBias(params.depthBiasConstantFactor,
                                    params.depthBiasSlopeFactor);
        renderPassState->SetDepthFunc(params.depthFunc);
        renderPassState->SetEnableDepthMask(params.depthMaskEnable);

        renderPassState->SetStencilEnabled(params.stencilEnable);
        renderPassState->SetStencil(params.stencilFunc, params.stencilRef,
                params.stencilMask, params.stencilFailOp, params.stencilZFailOp,
                params.stencilZPassOp);

        renderPassState->SetBlendEnabled(params.blendEnable);
        renderPassState->SetBlend(
                params.blendColorOp,
                params.blendColorSrcFactor, params.blendColorDstFactor,
                params.blendAlphaOp,
                params.blendAlphaSrcFactor, params.blendAlphaDstFactor);
        renderPassState->SetBlendConstantColor(params.blendConstantColor);

        // Don't enable alpha to coverage for id or non-color renders.
        // XXX: If the list of aovs includes both color and an id aov (such as
        // primdId), we still disable alpha to coverage for the render pass,
        // which may result in different behavior for the color output compared
        // to if the user didn't request an id aov at the same time.
        // If this becomes an issue, we'll need to reconsider this approach.
        renderPassState->SetAlphaToCoverageEnabled(
            params.enableAlphaToCoverage &&
            (usingNoAovs || usingColorAov) && !usingIdAov &&
            !TfDebug::IsEnabled(HDX_DISABLE_ALPHA_TO_COVERAGE));

        // For id renders that use an id aov (which use an int format), it's ok
        // to have multi-sampling enabled. During the MSAA resolve for integer
        // types, a single sample will be selected.
        renderPassState->SetMultiSampleEnabled(params.useAovMultiSample);

        if (HdStRenderPassState * const hdStRenderPassState =
                    dynamic_cast<HdStRenderPassState*>(renderPassState.get())) {
            hdStRenderPassState->SetUseSceneMaterials(
                params.enableSceneMaterials);
            
            // Don't enable multisample for id renders.
            hdStRenderPassState->SetUseAovMultiSample(
                params.useAovMultiSample);
            hdStRenderPassState->SetResolveAovMultiSample(
                params.resolveAovMultiSample);
        }
    }
}

void
HdxRenderSetupTask::_PrepareAovBindings(HdTaskContext* ctx,
                                        HdRenderIndex* renderIndex)
{
    // Walk the aov bindings, resolving the render index references as they're
    // encountered.
    //
    // This is somewhat fragile. One of the clients is _BindTexture in
    // hdSt/renderPassShader.cpp.
    //
    for (size_t i = 0; i < _aovBindings.size(); ++i) {
        if (_aovBindings[i].renderBuffer == nullptr) {
            _aovBindings[i].renderBuffer = static_cast<HdRenderBuffer*>(
                renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer,
                _aovBindings[i].renderBufferId));
        }
    }

    HdRenderPassStateSharedPtr &renderPassState =
            _GetRenderPassState(renderIndex);
    renderPassState->SetAovBindings(_aovBindings);
    renderPassState->SetAovInputBindings(_aovInputBindings);
}

void
HdxRenderSetupTask::PrepareCamera(HdRenderIndex* renderIndex)
{
    // If the render delegate does not support cameras, then
    // there is nothing to do here.
    if (!renderIndex->IsSprimTypeSupported(HdTokens->camera)) {
        return;
    } 

    const HdCamera * const camera =
        static_cast<const HdCamera *>(
            renderIndex->GetSprim(HdPrimTypeTokens->camera, _cameraId));

    HdRenderPassStateSharedPtr const &renderPassState =
            _GetRenderPassState(renderIndex);

    renderPassState->SetCamera(camera);
    renderPassState->SetOverrideWindowPolicy(_overrideWindowPolicy);

    if (_framing.IsValid()) {
        renderPassState->SetFraming(_framing);
    } else {
        renderPassState->SetViewport(_viewport);
    }
}

HdRenderPassStateSharedPtr &
HdxRenderSetupTask::_GetRenderPassState(HdRenderIndex* renderIndex)
{
    if (!_renderPassState) {
        HdRenderDelegate *renderDelegate = renderIndex->GetRenderDelegate();
        _renderPassState = renderDelegate->CreateRenderPassState();
    }

    return _renderPassState;
}

// --------------------------------------------------------------------------- //
// VtValue Requirements
// --------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const HdxRenderTaskParams& pv)
{
    out << "RenderTask Params: (...) " 
        << pv.overrideColor << " " 
        << pv.wireframeColor << " " 
        << pv.pointColor << " "
        << pv.pointSize << " "
        << pv.enableLighting << " "
        << pv.alphaThreshold << " "
        << pv.enableSceneMaterials << " "
        << pv.enableSceneLights << " "

        << pv.maskColor << " " 
        << pv.indicatorColor << " " 
        << pv.pointSelectedSize << " "

        << pv.depthBiasUseDefault << " "
        << pv.depthBiasEnable << " "
        << pv.depthBiasConstantFactor << " "
        << pv.depthBiasSlopeFactor << " "
        << pv.depthFunc << " "
        << pv.depthMaskEnable << " "
        << pv.stencilFunc << " "
        << pv.stencilRef << " "
        << pv.stencilMask << " "
        << pv.stencilFailOp << " "
        << pv.stencilZFailOp << " "
        << pv.stencilZPassOp << " "
        << pv.stencilEnable << " "
        << pv.blendColorOp << " "
        << pv.blendColorSrcFactor << " "
        << pv.blendColorDstFactor << " "
        << pv.blendAlphaOp << " "
        << pv.blendAlphaSrcFactor << " "
        << pv.blendAlphaDstFactor << " "
        << pv.blendConstantColor << " "
        << pv.blendEnable << " "
        << pv.enableAlphaToCoverage << ""
        << pv.useAovMultiSample << ""
        << pv.resolveAovMultiSample << ""

        << pv.camera << " "
        << pv.framing.displayWindow << " "
        << pv.framing.dataWindow << " "
        << pv.framing.pixelAspectRatio << " "
        << pv.viewport << " "
        << pv.cullStyle << " ";

    for (auto const& a : pv.aovBindings) {
        out << a << " ";
    }
    for (auto const& a : pv.aovInputBindings) {
        out << a << " (input) ";
    }
    return out;
}

bool operator==(const HdxRenderTaskParams& lhs, const HdxRenderTaskParams& rhs) 
{
    return lhs.overrideColor            == rhs.overrideColor            &&
           lhs.wireframeColor           == rhs.wireframeColor           &&
           lhs.pointColor               == rhs.pointColor               &&
           lhs.pointSize                == rhs.pointSize                &&
           lhs.enableLighting           == rhs.enableLighting           &&
           lhs.alphaThreshold           == rhs.alphaThreshold           &&
           lhs.enableSceneMaterials     == rhs.enableSceneMaterials     &&
           lhs.enableSceneLights        == rhs.enableSceneLights        &&
 
           lhs.maskColor                == rhs.maskColor                &&
           lhs.indicatorColor           == rhs.indicatorColor           &&
           lhs.pointSelectedSize        == rhs.pointSelectedSize        &&
 
           lhs.aovBindings              == rhs.aovBindings              &&
           lhs.aovInputBindings         == rhs.aovInputBindings         &&
           
           lhs.depthBiasUseDefault      == rhs.depthBiasUseDefault      &&
           lhs.depthBiasEnable          == rhs.depthBiasEnable          &&
           lhs.depthBiasConstantFactor  == rhs.depthBiasConstantFactor  &&
           lhs.depthBiasSlopeFactor     == rhs.depthBiasSlopeFactor     &&
           lhs.depthFunc                == rhs.depthFunc                &&
           lhs.depthMaskEnable          == rhs.depthMaskEnable          &&
           lhs.stencilFunc              == rhs.stencilFunc              &&
           lhs.stencilRef               == rhs.stencilRef               &&
           lhs.stencilMask              == rhs.stencilMask              &&
           lhs.stencilFailOp            == rhs.stencilFailOp            &&
           lhs.stencilZFailOp           == rhs.stencilZFailOp           &&
           lhs.stencilZPassOp           == rhs.stencilZPassOp           &&
           lhs.stencilEnable            == rhs.stencilEnable            &&
           lhs.blendColorOp             == rhs.blendColorOp             &&
           lhs.blendColorSrcFactor      == rhs.blendColorSrcFactor      &&
           lhs.blendColorDstFactor      == rhs.blendColorDstFactor      &&
           lhs.blendAlphaOp             == rhs.blendAlphaOp             &&
           lhs.blendAlphaSrcFactor      == rhs.blendAlphaSrcFactor      &&
           lhs.blendAlphaDstFactor      == rhs.blendAlphaDstFactor      &&
           lhs.blendConstantColor       == rhs.blendConstantColor       &&
           lhs.blendEnable              == rhs.blendEnable              &&
           lhs.enableAlphaToCoverage    == rhs.enableAlphaToCoverage    &&
           lhs.useAovMultiSample        == rhs.useAovMultiSample        &&
           lhs.resolveAovMultiSample    == rhs.resolveAovMultiSample    &&
           
           lhs.camera                   == rhs.camera                   &&
           lhs.framing                  == rhs.framing                  &&
           lhs.viewport                 == rhs.viewport                 &&
           lhs.cullStyle                == rhs.cullStyle                &&
           lhs.overrideWindowPolicy     == rhs.overrideWindowPolicy;
}

bool operator!=(const HdxRenderTaskParams& lhs, const HdxRenderTaskParams& rhs) 
{
    return !(lhs == rhs);
}

PXR_NAMESPACE_CLOSE_SCOPE
