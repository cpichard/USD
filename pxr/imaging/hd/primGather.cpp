//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/imaging/hd/primGather.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/work/dispatcher.h"
#include "pxr/base/work/withScopedParallelism.h"
#include <tbb/parallel_for.h>

PXR_NAMESPACE_OPEN_SCOPE

// Parallelism tunable values.
// Only run ranges in parallel if there are enough to cover the
// overhead.
static const size_t MIN_RANGES_FOR_PARALLEL  = 10;
static const size_t MIN_ENTRIES_FOR_PARALLEL = 10;
static const size_t MIN_GRAIN_SIZE           = 10;


void HdPrimGather::Filter(const SdfPathVector &paths,
                          const SdfPathVector &includePaths,
                          const SdfPathVector &excludePaths,
                          SdfPathVector       *results)
{
    HD_TRACE_FUNCTION();

    _SetupFilter(includePaths, excludePaths);
    _GatherPaths(paths);

    _WriteResults(paths,
                  _gatheredRanges.begin(),
                  _gatheredRanges.end(),
                  results);
}

void
HdPrimGather::PredicatedFilter(const SdfPathVector &paths,
                               const SdfPathVector &includePaths,
                               const SdfPathVector &excludePaths,
                               FilterPredicateFn    predicateFn,
                               void                *predicateParam,
                               SdfPathVector       *results)
{
    HD_TRACE_FUNCTION();

    _SetupFilter(includePaths, excludePaths);
    _GatherPaths(paths);

    {
        HD_TRACE_SCOPE("HdPrimGather::Predicate Test");

        size_t numRanges = _gatheredRanges.size();
        if (numRanges > MIN_RANGES_FOR_PARALLEL) {

            WorkWithScopedParallelism([&]() {
                WorkDispatcher rangeDispatcher;

                for (size_t rangeNum = 0; rangeNum < numRanges; ++rangeNum) {
                    const _Range &range = _gatheredRanges[rangeNum];

                    rangeDispatcher.Run(&HdPrimGather::_DoPredicateTestOnRange,
                                        this,
                                        std::cref(paths),
                                        range,
                                        predicateFn,
                                        predicateParam);
                }
            });
        } else {
            size_t numRanges = _gatheredRanges.size();
            for (size_t rangeNum = 0; rangeNum < numRanges; ++rangeNum) {
                const _Range &range = _gatheredRanges[rangeNum];

                _DoPredicateTestOnRange(paths,
                                        range,
                                        predicateFn,
                                        predicateParam);
            }
        }
    }

    typedef tbb::flattened2d<_ConcurrentRangeArray> _FlattenRangeArray;

    _FlattenRangeArray flattendResultRanges = tbb::flatten2d(_resultRanges);

    _WriteResults(paths,
                  flattendResultRanges.begin(),
                  flattendResultRanges.end(),
                  results);
}

void
HdPrimGather::Subtree(const SdfPathVector &paths,
                       const SdfPath       &rootPath,
                       SdfPathVector       *results)
{
    results->clear();

    _FilterSubTree(paths, rootPath);

    _WriteResults(paths,
                  _gatheredRanges.begin(),
                  _gatheredRanges.end(),
                  results);
}

bool
HdPrimGather::SubtreeAsRange(const SdfPathVector &paths,
                             const SdfPath       &rootPath,
                             size_t              *start,
                             size_t              *end)
{
    _FilterSubTree(paths, rootPath);

    if (_gatheredRanges.empty()) {
        return false;
    }

    if (_gatheredRanges.size() > 1) {
        TF_CODING_ERROR("Subtree produced more than 1 range.  List unsorted?");
        return false;
    }

    _Range &range = _gatheredRanges[0];
    *start = range._start;
    *end   = range._end;

    return true;
}

size_t
HdPrimGather::_FindLowerBound(const SdfPathVector &paths,
                               size_t start,
                               size_t end,
                               const SdfPath &path) const
{
    size_t rangeSize = end - start;
    while (rangeSize > 0) {
        size_t step = rangeSize / 2;
        size_t mid = start + step;

        const SdfPath &testPath = paths[mid];

        if ((testPath <  path)) {
            start = mid + 1;
            rangeSize -= step + 1;
        } else {
            rangeSize = step;
        }
    }

    return start;
}

size_t
HdPrimGather::_FindUpperBound(const SdfPathVector &paths,
                               size_t start,
                               size_t end,
                               const SdfPath &path) const
{
    // This code looks for the first index that doesn't have
    // the prefix, so special case if all paths have the prefix
    if (paths[end].HasPrefix(path)) {
        return end;
    }

    size_t rangeSize = end - start;
    while (rangeSize > 0) {
        size_t step = rangeSize / 2;
        size_t mid = start + step;

        const SdfPath &testPath = paths[mid];

        if (testPath.HasPrefix(path)) {
            start = mid + 1;
            rangeSize -= step + 1;
        } else {
            rangeSize = step;
        }
    }

    // start represents the first path that doens't have the prefix
    // but we want the inclusive range, so -1
    return start - 1;
}

// Apply the the top item on the filter stack to the range
// of elements between (start, end) (inclusive).
// isIncludeRange is the current state of the specified range.
void
HdPrimGather::_FilterRange(const SdfPathVector &paths,
                            size_t start,
                            size_t end,
                            bool isIncludeRange)
{
    std::vector< std::pair<_Range, bool> > ranges;
    ranges.push_back({{start, end}, isIncludeRange});

    while (!ranges.empty()) {
        // Pop the current range from the stack.
        start = ranges.back().first._start;
        end = ranges.back().first._end;
        isIncludeRange = ranges.back().second;
        ranges.pop_back();

        // If filter list is empty, we are done processing.
        if (_filterList.empty()) {
            if (isIncludeRange) {
                _gatheredRanges.emplace_back(start, end);
            }
            // Continue on to next range.
            continue;
        }

        // Take copy as we are going to pop_back before we use the filter.
        const _PathFilter currentFilter = _filterList.back();

        // Check to see if the top of the filter stack is beyond the
        // end of the range.  If it is, we are done processing this range.
        if (currentFilter._path > paths[end]) {
            if (isIncludeRange) {
                _gatheredRanges.emplace_back(start, end);
            }
            // Continue on to next range.
            continue;
        }

        // Need to process the filter, so remove it.
        _filterList.pop_back();

        // If the type of filter matches the range, it's a no-op.
        // Filter the same range again with the next filter.

        bool skipFilter = currentFilter._includePath == isIncludeRange;

        // Is filter before the start of the range?
        skipFilter |= (paths[start] > currentFilter._path) &&
                      (!paths[start].HasPrefix(currentFilter._path));

        if (skipFilter) {
            // Continue to process this range with the next filter.
            ranges.push_back({{start, end}, isIncludeRange});
            continue;
        }

        // We need to split the range.
        const size_t lowerBound = _FindLowerBound(paths,
                                            start,
                                            end,
                                            currentFilter._path);

        const size_t upperBound = _FindUpperBound(paths,
                                            lowerBound,
                                            end,
                                            currentFilter._path);

        // Push the ranges to process onto the stack.
        if (upperBound < end) {
            ranges.push_back({{upperBound+1, end}, isIncludeRange});
        }
        // Note: The type of range is inverted, because this is the
        // area that hit the filter.
        ranges.push_back({{lowerBound, upperBound}, !isIncludeRange});
        if (start < lowerBound) {
            ranges.push_back({{start, lowerBound-1}, isIncludeRange});
        }
    }
}


void
HdPrimGather::_SetupFilter(const SdfPathVector &includePaths,
                           const SdfPathVector &excludePaths)
{
    // Combine include and exclude paths in to the filter stack.
    _filterList.clear();
    _filterList.reserve(includePaths.size() + excludePaths.size());
    for (SdfPathVector::const_iterator incIt  = includePaths.begin();
                                       incIt != includePaths.end();
                                     ++incIt) {
        _filterList.emplace_back(*incIt, true);
    }

    for (SdfPathVector::const_iterator excIt  = excludePaths.begin();
                                       excIt != excludePaths.end();
                                     ++excIt) {
        _filterList.emplace_back(*excIt, false);
    }

    // Note: Inverted sort, so can pop back.
    std::sort(_filterList.begin(),
              _filterList.end(),
              std::greater<_PathFilter>());
}

void
HdPrimGather::_GatherPaths(const SdfPathVector &paths)
{
    // There is an expectation that paths is pre-sorted, but it is an
    // expensive check so only do it if safe mode is enabled.
    if (TfDebug::IsEnabled(HD_SAFE_MODE)) {
        TF_VERIFY(std::is_sorted(paths.begin(), paths.end()));
        // The side effect of not sorting is incorrect results of the
        // gather.  That should not lead to a crash, so just continue -
        // thus producing the same results as when safe mode is off.
    }

    _gatheredRanges.clear();

    if (paths.empty()) {
        return;
    }

    // Optimize the common case of including everything
    if ((_filterList.size() == 1) &&
        (_filterList[0]._includePath == true) &&
        (_filterList[0]._path == SdfPath::AbsoluteRootPath())) {
            // End of range is the inclusive.
            _gatheredRanges.emplace_back(0, paths.size() - 1);
            return;
    }

    // Enter recursive function
    // End of range is the inclusive.
    // We start with everything excluded from the results
    _FilterRange(paths, 0, paths.size() - 1, false);
}

// Outer Loop called for each range in vector
void
HdPrimGather::_DoPredicateTestOnRange(const SdfPathVector &paths,
                                      const _Range        &range,
                                      FilterPredicateFn    predicateFn,
                                      void                *predicateParam)
{
    TRACE_FUNCTION();

    // Range _end is inclusive, but blocked_range is exclusive.
    _ConcurrentRange concurrentRange(range._start,
                                     range._end + 1,
                                     MIN_GRAIN_SIZE);


    if (concurrentRange.size() > MIN_ENTRIES_FOR_PARALLEL) {

        tbb::parallel_for(concurrentRange,
                          std::bind(&HdPrimGather::_DoPredicateTestOnPrims,
                                    this,
                                    std::cref(paths),
                                    std::placeholders::_1,
                                    predicateFn,
                                    predicateParam));
    } else {
        _DoPredicateTestOnPrims(paths,
                                concurrentRange,
                                predicateFn,
                                predicateParam);
    }
}

// Inner Loop over each prim in a sub range of _Range.
void
HdPrimGather::_DoPredicateTestOnPrims(const SdfPathVector &paths,
                               _ConcurrentRange    &range,
                               FilterPredicateFn    predicateFn,
                               void                *predicateParam)
{
    TRACE_FUNCTION();

    size_t begin = range.begin();
    size_t end   = range.end() - 1;  // convert to inclusive.

    _RangeArray &resultRanges = _resultRanges.local();


    size_t currentStart = begin;
    for (size_t pathIdx = begin; pathIdx <= end; ++pathIdx) {
        // Test to see if path at index needs to split
        if (!predicateFn(paths[pathIdx], predicateParam)) {

            // Add all paths up to the path before this one
            if (currentStart < pathIdx) {
                resultRanges.emplace_back(currentStart, pathIdx - 1);
            }

            currentStart = pathIdx + 1;
        }
    }

    // Add final range
    if (currentStart <= end) {
        resultRanges.emplace_back(currentStart, end);
    }
}

template <class Iterator>
//static
void
HdPrimGather::_WriteResults(const SdfPathVector &paths,
                            const Iterator &rangesBegin,
                            const Iterator &rangesEnd,
                            SdfPathVector *results)
{
    results->clear();

    size_t numPaths = 0;
    for (Iterator it  = rangesBegin; it != rangesEnd; ++it) {
        numPaths += (it->_end - it->_start) + 1; // +1 for inclusive range.
    }

    results->reserve(numPaths);

    for (Iterator it  = rangesBegin; it != rangesEnd; ++it) {

        SdfPathVector::const_iterator rangeStartIt = paths.begin();
        SdfPathVector::const_iterator rangeEndIt   = paths.begin();

        rangeStartIt += it->_start;
        rangeEndIt   += it->_end + 1; // End is exclusive, so +1

        results->insert(results->end(), rangeStartIt, rangeEndIt);
    }
}


void
HdPrimGather::_FilterSubTree(const SdfPathVector &paths,
                             const SdfPath       &rootPath)
{
    if (paths.empty()) {
        return;
    }

    // Setup simple filter
    _filterList.clear();
    _filterList.reserve(1);
    _filterList.emplace_back(rootPath, true);

    // Enter filter function
    // End of range is the inclusive.
    // We start with everything excluded from the results
    _FilterRange(paths, 0, paths.size() - 1, false);
}

PXR_NAMESPACE_CLOSE_SCOPE
