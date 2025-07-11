//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/pcp/arc.h"
#include "pxr/usd/pcp/cache.h"
#include "pxr/usd/pcp/diagnostic.h"
#include "pxr/usd/pcp/errors.h"
#include "pxr/usd/pcp/iterator.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/pcp/node.h"
#include "pxr/usd/pcp/primIndex.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/site.h"
#include "pxr/usd/sdf/siteUtils.h"

#include "pxr/base/tf/stringUtils.h"

#include <fstream>
#include <iostream>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

static void
_ValidateAndPrintNode(
    std::ostream& out, const PcpNodeRef& node)
{
    TF_AXIOM(node);

    out << Pcp_FormatSite(node.GetSite())
        << "\t"
        << TfEnum::GetDisplayName(node.GetArcType()).c_str()
        ;
}

static void
_ValidateAndPrintPrimFromNode(
    std::ostream& out, 
    const SdfSite& sdSite,
    const PcpNodeRef& node)
{
    TF_AXIOM(SdfGetPrimAtPath(sdSite));
    TF_AXIOM(node);
    TF_AXIOM(node.GetSite().path == sdSite.path);
    TF_AXIOM(node.GetSite().layerStack->GetIdentifier().rootLayer == 
             sdSite.layer);

    out << Pcp_FormatSite(node.GetSite())
        << "\t"
        << TfEnum::GetDisplayName(node.GetArcType()).c_str()
        ;
}

static std::string
_GetRangeType(PcpRangeType type)
{
    return TfEnum::GetDisplayName(type);
}

static void
_IterateAndPrintPrimIndexNodes(
    std::ostream& out,
    PcpCache* cache, 
    const PcpPrimIndex& primIndex,
    PcpRangeType type = PcpRangeTypeAll)
{
    out << "Iterating over " << _GetRangeType(type) << " nodes for " 
        << TfStringPrintf("<%s>:", primIndex.GetRootNode().GetSite().path.GetText())
        << std::endl;

    TF_FOR_ALL(it, primIndex.GetNodeRange(type)) {
        out << " ";
        _ValidateAndPrintNode(out, *it);
        out << std::endl;
    }

    out << std::endl;
    out << "Reverse iterating over " << _GetRangeType(type) << " nodes for " 
        << TfStringPrintf("<%s>:", primIndex.GetRootNode().GetSite().path.GetText())
        << std::endl;

    TF_REVERSE_FOR_ALL(it, primIndex.GetNodeRange(type)) {
        out << " ";
        _ValidateAndPrintNode(out, *it);
        out << std::endl;
    }
}

static void 
_IterateAndPrintPrimIndexPrims(
    std::ostream& out,
    PcpCache* cache,
    const PcpPrimIndex& primIndex,
    PcpRangeType type = PcpRangeTypeAll)
{
    out << "Iterating over " << _GetRangeType(type) << " prim specs for " 
        << TfStringPrintf("<%s>:", primIndex.GetRootNode().GetSite().path.GetText())
        << std::endl;

    TF_FOR_ALL(it, primIndex.GetPrimRange(type)) {
        out << " ";
        _ValidateAndPrintPrimFromNode(out, *it, it.base().GetNode());
        out << std::endl;
    }

    out << std::endl;
    out << "Reverse iterating over " << _GetRangeType(type) << " prim specs for " 
        << TfStringPrintf("<%s>:", primIndex.GetRootNode().GetSite().path.GetText())
        << std::endl;

    TF_REVERSE_FOR_ALL(it, primIndex.GetPrimRange(type)) {
        out << " ";
        _ValidateAndPrintPrimFromNode(out, *it, it.base().GetNode());
        out << std::endl;
    }
}

static void
_IterateAndPrintPrimIndex(
    std::ostream& out,
    PcpCache* cache,
    const SdfPath& primPath,
    PcpRangeType type = PcpRangeTypeAll)
{
    PcpErrorVector errors;
    const PcpPrimIndex& primIndex = cache->ComputePrimIndex(primPath, &errors);
    PcpRaiseErrors(errors);

    _IterateAndPrintPrimIndexNodes(out, cache, primIndex, type);
    out << std::endl;
    _IterateAndPrintPrimIndexPrims(out, cache, primIndex, type);
}

static void
_IterateAndPrintPrimIndexSubtreeRanges(
    std::ostream& out,
    PcpCache* cache,
    const SdfPath& primPath)
{
    PcpErrorVector errors;
    const PcpPrimIndex& primIndex = cache->ComputePrimIndex(primPath, &errors);
    PcpRaiseErrors(errors);

    for (const auto &node : primIndex.GetNodeRange()) {
        out << std::endl;
        out << "Subtree iterating over subtree nodes starting at node " 
            << Pcp_FormatSite(node.GetSite()) << ":"
            << std::endl;

        for (const PcpNodeRef &subtreeNode : primIndex.GetNodeSubtreeRange(node)) {
            out << " ";
            _ValidateAndPrintNode(out, subtreeNode);
            out << std::endl;
        }
    }
}


static void
_IterateAndPrintPropertyIndex(
    std::ostream& out,
    PcpCache* cache,
    const SdfPath& propPath,
    bool localOnly)
{
    PcpErrorVector errors;
    const PcpPropertyIndex& propIndex = 
        cache->ComputePropertyIndex(propPath, &errors);
    PcpRaiseErrors(errors);

    out << "Iterating over " << (localOnly ? "local" : "all") 
        << " property specs for " << TfStringPrintf("<%s>:", propPath.GetText())
        << std::endl;

    TF_FOR_ALL(it, propIndex.GetPropertyRange(localOnly)) {
        out << " "
            << Pcp_FormatSite(PcpSite((*it)->GetLayer(), (*it)->GetPath()))
            << " from node ";
        _ValidateAndPrintNode(out, it.base().GetNode());
        out << std::endl;
    }

    out << std::endl;

    out << "Reverse iterating over " << (localOnly ? "local" : "all") 
        << " property specs for " << TfStringPrintf("<%s>:", propPath.GetText())
        << std::endl;

    TF_REVERSE_FOR_ALL(it, propIndex.GetPropertyRange(localOnly)) {
        out << " "
            << Pcp_FormatSite(PcpSite((*it)->GetLayer(), (*it)->GetPath()))
            << " from node ";
        _ValidateAndPrintNode(out, it.base().GetNode());
        out << std::endl;
    }
}

template <class IteratorType>
static void
_TestComparisonOperations(IteratorType first, IteratorType last)
{
    TF_AXIOM(first != last);

    IteratorType first2 = first, last2 = last;
    do {
        TF_AXIOM(first == first2);
        ++first;
        TF_AXIOM(first != first2);
        ++first2;
    }
    while (first != last && first2 != last2);
}

template <class IteratorType>
static void
_TestRandomAccessOperations(IteratorType first, IteratorType last)
{
    TF_AXIOM(first != last);

    typename std::iterator_traits<IteratorType>::difference_type idx = 0;
    for (IteratorType it = first; it != last; ++it, ++idx) {
        TF_AXIOM(it - first == idx);
        TF_AXIOM(it - idx == first);
        TF_AXIOM(it == first + idx);
    }
}

// Ensure that using increment/decrement operators and std::prev / std::next
// produce symmetrical results
template <class IteratorType>
static void
_TestIncrementAndAdvanceSymmetry(IteratorType first, IteratorType last)
{
    TF_AXIOM(first != last);
    TF_AXIOM(std::distance(first, last) > 2);

    IteratorType byIncrement = first;
    ++byIncrement;
    ++byIncrement;
    --byIncrement;

    IteratorType byAdvance = std::prev(std::next(first, 2));

    TF_AXIOM(std::distance(first, byIncrement) == 1);
    TF_AXIOM(std::distance(first, byAdvance) == 1);
    TF_AXIOM(std::distance(byIncrement, first) == -1);
    TF_AXIOM(std::distance(byAdvance, first) == -1);
    TF_AXIOM(std::distance(byAdvance, byIncrement) == 0);
    TF_AXIOM(byIncrement == byAdvance);
}

static std::unique_ptr<PcpCache>
_CreateCacheForRootLayer(const std::string& rootLayerPath)
{
    SdfLayerRefPtr rootLayer = SdfLayer::FindOrOpen(rootLayerPath);
    if (!rootLayer) {
        return std::unique_ptr<PcpCache>();
    }

    const PcpLayerStackIdentifier layerStackID(
        rootLayer, SdfLayerRefPtr(), ArResolverContext());
    return std::make_unique<PcpCache>(layerStackID);
}

int 
main(int argc, char** argv)
{
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: " << std::endl;
        std::cerr << argv[0] << std::endl;
        std::cerr << "\tRuns standard built-in tests" << std::endl;
        std::cerr << argv[0] << " root_layer prim_path" << std::endl;
        std::cerr << "\tPrints results of iteration over prim_path in scene "
                  << "with given root_layer" << std::endl;
        std::cerr << "\tex: " << argv[0] << " root.usda /Model" << std::endl;
        return EXIT_FAILURE;
    }

    // Handle case where user specifies root layer and prim path to iterate
    // over.
    if (argc == 3) {
        const std::string layerPath(argv[1]);
        const SdfPath primPath(argv[2]);

        std::unique_ptr<PcpCache> cache = _CreateCacheForRootLayer(layerPath);
        if (!cache) {
            std::cerr << "Failed to load root layer " << layerPath << std::endl;
            return EXIT_FAILURE;
        }

        _IterateAndPrintPrimIndex(std::cout, cache.get(), primPath);
        return EXIT_SUCCESS;
    }

    // Otherwise, run the normal test suite.
    std::unique_ptr<PcpCache> cache = _CreateCacheForRootLayer("root.usda");
    TF_AXIOM(cache);

    SdfPathSet includePayload;
    includePayload.insert(SdfPath("/Model"));
    cache->RequestPayloads(includePayload, SdfPathSet());

    std::cout << "Testing comparison operators..." << std::endl;
    {
        PcpErrorVector errors;
        const PcpPrimIndex& primIndex = 
            cache->ComputePrimIndex(SdfPath("/Model"), &errors);
        PcpRaiseErrors(errors);

        const PcpNodeRange nodeRange = primIndex.GetNodeRange();
        _TestComparisonOperations(nodeRange.first, nodeRange.second);

        const PcpPrimRange primRange = primIndex.GetPrimRange();
        _TestComparisonOperations(primRange.first, primRange.second);

        const PcpPropertyIndex& propIndex = 
            cache->ComputePropertyIndex(SdfPath("/Model.a"), &errors);
        PcpRaiseErrors(errors);

        const PcpPropertyRange propRange = propIndex.GetPropertyRange();
        _TestComparisonOperations(propRange.first, propRange.second);
    }

    std::cout << "Testing Increment / Advance Symmetry" << std::endl;
    {
        PcpErrorVector errors;
        const PcpPrimIndex& primIndex =
            cache->ComputePrimIndex(SdfPath("/Model"), &errors);
        PcpRaiseErrors(errors);

        const PcpNodeRange nodeRange = primIndex.GetNodeRange();
        _TestIncrementAndAdvanceSymmetry(nodeRange.first, nodeRange.second);

        const PcpPrimRange primRange = primIndex.GetPrimRange();
        _TestIncrementAndAdvanceSymmetry(primRange.first, primRange.second);

        const PcpPropertyIndex& propIndex =
            cache->ComputePropertyIndex(SdfPath("/Model.a"), &errors);
        PcpRaiseErrors(errors);

        const PcpPropertyRange propRange = propIndex.GetPropertyRange();
        _TestIncrementAndAdvanceSymmetry(propRange.first, propRange.second);
    }

    std::cout << "Testing random access operations..." << std::endl;
    {
        PcpErrorVector errors;
        const PcpPrimIndex& primIndex = 
            cache->ComputePrimIndex(SdfPath("/Model"), &errors);
        PcpRaiseErrors(errors);

        const PcpNodeRange nodeRange = primIndex.GetNodeRange();
        _TestRandomAccessOperations(nodeRange.first, nodeRange.second);
        _TestRandomAccessOperations(
            PcpNodeReverseIterator(nodeRange.second),
            PcpNodeReverseIterator(nodeRange.first));

        const PcpPrimRange primRange = primIndex.GetPrimRange();
        _TestRandomAccessOperations(primRange.first, primRange.second);
        _TestRandomAccessOperations(
            PcpPrimReverseIterator(primRange.second),
            PcpPrimReverseIterator(primRange.first));

        const PcpPropertyIndex& propIndex = 
            cache->ComputePropertyIndex(SdfPath("/Model.a"), &errors);
        PcpRaiseErrors(errors);

        const PcpPropertyRange propRange = propIndex.GetPropertyRange();
        _TestRandomAccessOperations(propRange.first, propRange.second);
        _TestRandomAccessOperations(
            PcpPropertyReverseIterator(propRange.second),
            PcpPropertyReverseIterator(propRange.first));
    }

    std::cout << "Testing GetNodeIteratorAtNode" << std::endl;
    {
        PcpErrorVector errors;
        const PcpPrimIndex& primIndex = 
            cache->ComputePrimIndex(SdfPath("/Model"), &errors);
        PcpRaiseErrors(errors);

        const PcpNodeRange nodeRange = primIndex.GetNodeRange();
        for (PcpNodeIterator it = nodeRange.first; 
                it != nodeRange.second; ++it) {
            PcpNodeRef node = *it;
            PcpNodeIterator iteratorAtNode = 
                primIndex.GetNodeIteratorAtNode(node);
            TF_AXIOM(it == iteratorAtNode);
        }

        TF_AXIOM(
            primIndex.GetNodeIteratorAtNode(PcpNodeRef()) == nodeRange.second);
    }

    std::cout << "Testing iteration (output to file)..." << std::endl;
    {
        std::ofstream outfile("iteration_results.txt");
        for (PcpRangeType type = PcpRangeTypeRoot;
             type != PcpRangeTypeInvalid;
             type = (PcpRangeType)(type + 1)) {

            _IterateAndPrintPrimIndex(
                outfile, cache.get(), SdfPath("/Model"), type);
            outfile << std::endl 
                    << "====================" << std::endl 
                    << std::endl;
        }

        _IterateAndPrintPrimIndexSubtreeRanges(
            outfile, cache.get(), SdfPath("/Model"));

        outfile << std::endl 
                << "====================" << std::endl 
                << std::endl;

        _IterateAndPrintPropertyIndex(
            outfile, cache.get(), SdfPath("/Model.a"), /* localOnly */ true);

        outfile << std::endl 
                << "====================" << std::endl 
                << std::endl;

        _IterateAndPrintPropertyIndex(
            outfile, cache.get(), SdfPath("/Model.a"), /* localOnly */ false);
    }
    
    return EXIT_SUCCESS;
}
