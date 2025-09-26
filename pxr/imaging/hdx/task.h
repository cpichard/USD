//
// Copyright 2018 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HDX_TASK_H
#define PXR_IMAGING_HDX_TASK_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdx/api.h"
#include "pxr/imaging/hd/task.h"

PXR_NAMESPACE_OPEN_SCOPE

class Hgi;


/// \class HdxTask
///
/// Base class for (some) tasks in Hdx that provides common progressive 
/// rendering and Hgi functionality.
///
/// Tasks that require neither progressive rendering nor Hgi can continue to
/// derive directly from HdTask.
///
class HdxTask : public HdTask
{
public:
    HDX_API
    HdxTask(SdfPath const& id);

    HDX_API
    ~HdxTask() override;

    /// Are all tasks at taskPaths in the render index converged?
    static
    HDX_API
    bool AreTasksConverged(HdRenderIndex * const renderIndex,
                           const SdfPathVector &taskPaths);

    /// We override HdTask::Sync, but make it 'final' to make sure derived
    /// classes can't override it and instead override _Sync.
    /// This 'non-virtual interface'-like pattern allows us to ensure we always
    /// initialized Hgi during the Sync task so derived classes don't have to.
    HDX_API 
    void Sync(
        HdSceneDelegate* delegate,
        HdTaskContext* ctx,
        HdDirtyBits* dirtyBits) override final;

protected:
    // This is called during the hydra Sync Phase via HdxTask::Sync.
    // Please see HdTask::Sync for Sync Phase documentation.
    virtual void _Sync(
        HdSceneDelegate* delegate,
        HdTaskContext* ctx,
        HdDirtyBits* dirtyBits) = 0;

    // Swaps the color target and colorIntermediate target.
    // This is used when a task wishes to read from the color and also write
    // to it. We use two color targets and ping-pong between them.
    HDX_API
    void _ToggleRenderTarget(HdTaskContext* ctx);

    // Swaps the depth target and depthIntermediate target.
    // This is used when a task wishes to read from the depth and also write
    // to it. We use two depth targets and ping-pong between them.
    HDX_API
    void _ToggleDepthTarget(HdTaskContext* ctx);

    // Helper function to facilitate texture ping-ponging.
    HDX_API
    void _SwapTextures(
        HdTaskContext* ctx,
        const TfToken& textureToken,
        const TfToken& textureIntermediateToken);

    // Return pointer to Hydra Graphics Interface.
    HDX_API
    Hgi* _GetHgi() const;

private:
    Hgi* _hgi;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

