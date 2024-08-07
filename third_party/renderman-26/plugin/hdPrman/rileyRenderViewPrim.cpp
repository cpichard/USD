//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#include "hdPrman/rileyRenderViewPrim.h"

#ifdef HDPRMAN_USE_SCENE_INDEX_OBSERVER

#include "hdPrman/rileyIds.h"
#include "hdPrman/rileyTypes.h"

#include "hdPrman/rileyRenderViewSchema.h"
#include "hdPrman/rileyCameraPrim.h"
#include "hdPrman/rileyDisplayFilterPrim.h"
#include "hdPrman/rileyIntegratorPrim.h"
#include "hdPrman/rileyRenderTargetPrim.h"
#include "hdPrman/rileySampleFilterPrim.h"
#include "hdPrman/utils.h"

#include "pxr/imaging/hd/sceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

HdPrman_RileyRenderViewPrim::HdPrman_RileyRenderViewPrim(
    HdContainerDataSourceHandle const &primSource,
    const HdsiPrimManagingSceneIndexObserver * const observer,
    HdPrman_RenderParam * const renderParam)
  : HdPrman_RileyPrimBase(renderParam)
{
    HdPrmanRileyRenderViewSchema schema =
        HdPrmanRileyRenderViewSchema::GetFromParent(primSource);


    HdPrman_RileyId<HdPrman_RileyRenderTargetPrim> renderTarget =
        HdPrman_RileyId<HdPrman_RileyRenderTargetPrim>(
            observer,
            schema.GetRenderTarget());

    HdPrman_RileyId<HdPrman_RileyCameraPrim> camera =
        HdPrman_RileyId<HdPrman_RileyCameraPrim>(
            observer,
            schema.GetCamera());

    HdPrman_RileyId<HdPrman_RileyIntegratorPrim> integrator =
        HdPrman_RileyId<HdPrman_RileyIntegratorPrim>(
            observer,
            schema.GetIntegrator());

    HdPrman_RileyIdList<HdPrman_RileyDisplayFilterPrim> displayFilters =
        HdPrman_RileyIdList<HdPrman_RileyDisplayFilterPrim>(
            observer,
            schema.GetDisplayFilters());

    HdPrman_RileyIdList<HdPrman_RileySampleFilterPrim> sampleFilters =
        HdPrman_RileyIdList<HdPrman_RileySampleFilterPrim>(
            observer,
            schema.GetSampleFilters());

    HdPrman_RileyParamList params =
        HdPrman_RileyParamList(
            schema.GetParams());


    _rileyId = _AcquireRiley()->CreateRenderView(
        riley::UserId(),

        renderTarget.rileyObject,
        camera.rileyObject,
        integrator.rileyObject,
        displayFilters.rileyObject,
        sampleFilters.rileyObject,
        params.rileyObject);

    _renderTargetPrim = std::move(renderTarget.prim);

    _cameraPrim = std::move(camera.prim);

    _integratorPrim = std::move(integrator.prim);

    _displayFiltersPrims = std::move(displayFilters.prims);

    _sampleFiltersPrims = std::move(sampleFilters.prims);

// --(BEGIN CUSTOM CODE: Constructor)--
// --(END CUSTOM CODE: Constructor)--
}

void
HdPrman_RileyRenderViewPrim::_Dirty(
    const HdSceneIndexObserver::DirtiedPrimEntry &entry,
    const HdsiPrimManagingSceneIndexObserver * const observer)
{
    HdPrmanRileyRenderViewSchema schema =
        HdPrmanRileyRenderViewSchema::GetFromParent(
            observer->GetSceneIndex()->GetPrim(entry.primPath).dataSource);


    std::optional<HdPrman_RileyId<HdPrman_RileyRenderTargetPrim>> renderTarget;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetRenderTargetLocator())) {
        renderTarget =
            HdPrman_RileyId<HdPrman_RileyRenderTargetPrim>(
                observer,
                schema.GetRenderTarget());
    };

    std::optional<HdPrman_RileyId<HdPrman_RileyCameraPrim>> camera;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetCameraLocator())) {
        camera =
            HdPrman_RileyId<HdPrman_RileyCameraPrim>(
                observer,
                schema.GetCamera());
    };

    std::optional<HdPrman_RileyId<HdPrman_RileyIntegratorPrim>> integrator;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetIntegratorLocator())) {
        integrator =
            HdPrman_RileyId<HdPrman_RileyIntegratorPrim>(
                observer,
                schema.GetIntegrator());
    };

    std::optional<HdPrman_RileyIdList<HdPrman_RileyDisplayFilterPrim>> displayFilters;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetDisplayFiltersLocator())) {
        displayFilters =
            HdPrman_RileyIdList<HdPrman_RileyDisplayFilterPrim>(
                observer,
                schema.GetDisplayFilters());
    };

    std::optional<HdPrman_RileyIdList<HdPrman_RileySampleFilterPrim>> sampleFilters;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetSampleFiltersLocator())) {
        sampleFilters =
            HdPrman_RileyIdList<HdPrman_RileySampleFilterPrim>(
                observer,
                schema.GetSampleFilters());
    };

    std::optional<HdPrman_RileyParamList> params;
    if (entry.dirtyLocators.Intersects(
            HdPrmanRileyRenderViewSchema::GetParamsLocator())) {
        params =
            HdPrman_RileyParamList(
                schema.GetParams());
    };


    _AcquireRiley()->ModifyRenderView(_rileyId,
        HdPrman_GetRileyObjectPtr(renderTarget),
        HdPrman_GetRileyObjectPtr(camera),
        HdPrman_GetRileyObjectPtr(integrator),
        HdPrman_GetRileyObjectPtr(displayFilters),
        HdPrman_GetRileyObjectPtr(sampleFilters),
        HdPrman_GetRileyObjectPtr(params)
// --(BEGIN CUSTOM CODE: Modify)--
// --(END CUSTOM CODE: Modify)--
        );


    if (renderTarget) {
        _renderTargetPrim = std::move(renderTarget->prim);
    }

    if (camera) {
        _cameraPrim = std::move(camera->prim);
    }

    if (integrator) {
        _integratorPrim = std::move(integrator->prim);
    }

    // Now that the RenderView is using the new
    // RileyDisplayFilter, we can realease the handles
    // to the old RileyDisplayFilter.
    if (displayFilters) {
        _displayFiltersPrims = std::move(displayFilters->prims);
    }

    // Now that the RenderView is using the new
    // RileySampleFilter, we can realease the handles
    // to the old RileySampleFilter.
    if (sampleFilters) {
        _sampleFiltersPrims = std::move(sampleFilters->prims);
    }

}

HdPrman_RileyRenderViewPrim::~HdPrman_RileyRenderViewPrim()
{
    _AcquireRiley()->DeleteRenderView(_rileyId
// --(BEGIN CUSTOM CODE: Delete)--
// --(END CUSTOM CODE: Delete)--
        );


    // _renderTargetPrim gets dropped after the RenderView was
    // deleted.

    // _cameraPrim gets dropped after the RenderView was
    // deleted.

    // _integratorPrim gets dropped after the RenderView was
    // deleted.

    // _displayFiltersPrims gets dropped after the RenderView was
    // deleted.

    // _sampleFiltersPrims gets dropped after the RenderView was
    // deleted.
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // #ifdef HDPRMAN_USE_SCENE_INDEX_OBSERVER