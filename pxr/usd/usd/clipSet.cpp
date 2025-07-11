//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/sdf/usdaFileFormat.h"
#include "pxr/usd/usd/clipSet.h"
#include "pxr/usd/usd/clipsAPI.h"
#include "pxr/usd/usd/clipSetDefinition.h"
#include "pxr/usd/usd/debugCodes.h"
#include "pxr/usd/usd/valueUtils.h"

#include "pxr/usd/pcp/layerStack.h"

#include "pxr/base/tf/staticTokens.h"

#include <algorithm>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (generated_manifest)
    );

bool
Usd_IsAutoGeneratedClipManifest(const SdfLayerHandle& manifestLayer)
{
    return manifestLayer->IsAnonymous() && 
        TfStringContains(
            manifestLayer->GetIdentifier(),
            _tokens->generated_manifest.GetString());
}

SdfLayerRefPtr
Usd_GenerateClipManifest(
    const Usd_ClipRefPtrVector& clips, const SdfPath& clipPrimPath,
    const std::string& tag,
    bool writeBlocksForClipsWithMissingValues)
{
    TRACE_FUNCTION();

    SdfLayerHandleVector clipLayers;
    clipLayers.reserve(clips.size());

    std::vector<double> activeTimes;
    activeTimes.reserve(clips.size());

    for (const Usd_ClipRefPtr& clip : clips) {
        const SdfLayerHandle clipLayer = clip->GetLayer();
        if (clipLayer) {
            clipLayers.push_back(clipLayer);
            activeTimes.push_back(clip->authoredStartTime);
        }
    }

    return Usd_GenerateClipManifest(
        clipLayers, clipPrimPath, tag,
        writeBlocksForClipsWithMissingValues ? &activeTimes : nullptr);
}

SdfLayerRefPtr
Usd_GenerateClipManifest(
    const SdfLayerHandleVector& clipLayers, const SdfPath& clipPrimPath,
    const std::string& tag,
    const std::vector<double>* clipActive)
{
    TRACE_FUNCTION();

    if (!clipPrimPath.IsPrimPath()) {
        TF_CODING_ERROR("<%s> must be a prim path", clipPrimPath.GetText());
        return SdfLayerRefPtr();
    }

    if (std::any_of(
            clipLayers.begin(), clipLayers.end(), 
            [](const SdfLayerHandle& l) { return !l; })) {
        TF_CODING_ERROR("Invalid clip layer");
        return SdfLayerRefPtr();
    }

    SdfLayerRefPtr manifestLayer = SdfLayer::CreateAnonymous(TfStringPrintf(
        "%s.%s", tag.c_str(), SdfUsdaFileFormatTokens->Id.GetText()));
 
    SdfChangeBlock changeBlock;
   
    for (const SdfLayerHandle& clipLayer : clipLayers) {
        clipLayer->Traverse(
            clipPrimPath,
            [&manifestLayer, &clipLayer](const SdfPath& path) {
                if (!path.IsPropertyPath()) {
                    return;
                }
                // This code can be pretty hot so we want to 1, avoid the
                // LayerHandle dereference cost more than once and 2, avoid the
                // SdfSpec API (e.g. layer->GetAttributeAtPath()) since it has
                // to take the layer's identity registry lock to produce the
                // spec handle.
                SdfLayer *layerPtr = get_pointer(clipLayer);
                TfToken typeName;
                SdfVariability variability;
                if (!manifestLayer->HasSpec(path)
                    && layerPtr->GetSpecType(path) == SdfSpecTypeAttribute
                    && layerPtr->HasField(
                        path, SdfFieldKeys->TypeName, &typeName)
                    && layerPtr->HasField(
                        path, SdfFieldKeys->Variability, &variability)
                    && clipLayer->GetNumTimeSamplesForPath(path) != 0) {
                    SdfJustCreatePrimAttributeInLayer(
                        manifestLayer, path,
                        layerPtr->GetSchema().FindType(typeName),
                        variability);
                }
            });
    }

    if (clipActive) {
        std::vector<std::pair<SdfPath, std::vector<double>>>
            attrToActiveTimeOfClipsWithoutSamples;

        manifestLayer->Traverse(
            clipPrimPath,
            [&clipLayers, &clipActive,
             &attrToActiveTimeOfClipsWithoutSamples](const SdfPath& path) {
                
                if (!path.IsPropertyPath()) {
                    return;
                }

                // We've only added attributes to the manifest layer above,
                // so if we're here we know the path must be an attribute.
                // Check if this attribute has time samples in all clips,
                // recording the activation time of the clips that don't.
                std::vector<double> activeTimes;

                for (size_t i = 0; i < clipLayers.size(); ++i) {
                    const SdfLayerHandle& clipLayer = clipLayers[i];
                    if (clipLayer->GetNumTimeSamplesForPath(path) == 0) {
                        activeTimes.push_back((*clipActive)[i]);
                    }
                }

                if (!activeTimes.empty()) {
                    attrToActiveTimeOfClipsWithoutSamples.emplace_back(
                        path, std::move(activeTimes));
                }
            });

        SdfValueBlock block;

        for (auto pathAndActiveTime : attrToActiveTimeOfClipsWithoutSamples) {
            for (double time : pathAndActiveTime.second) {
                manifestLayer->SetTimeSample(
                    pathAndActiveTime.first, time, block);
            }
        }
    }

    return manifestLayer;
}

// ------------------------------------------------------------

static bool
_ValidateClipFields(
    const VtArray<SdfAssetPath>& clipAssetPaths,
    const std::string& clipPrimPath,
    const VtVec2dArray& clipActive,
    const VtVec2dArray* clipTimes,
    std::string* errMsg)
{
    // Note that we do allow empty clipAssetPath and clipActive data; 
    // this provides users with a way to 'block' clips specified in a 
    // weaker layer.
    if (clipPrimPath.empty()) {
        *errMsg = TfStringPrintf(
            "No clip prim path specified in '%s'",
            UsdClipsAPIInfoKeys->primPath.GetText());
        return false;
    }

    const size_t numClips = clipAssetPaths.size();

    // Each entry in the clipAssetPaths array is the asset path to a clip.
    for (const auto& clipAssetPath : clipAssetPaths) {
        if (clipAssetPath.GetAssetPath().empty()) {
            *errMsg = TfStringPrintf(
                "Empty clip asset path in '%s'",
                UsdClipsAPIInfoKeys->assetPaths.GetText());
            return false;
        }
    }

    // The 'clipPrimPath' field identifies a prim from which clip data
    // will be read.
    if (!SdfPath::IsValidPathString(clipPrimPath, errMsg)) {
        return false;
    }
    
    const SdfPath path(clipPrimPath);
    if (!(path.IsAbsolutePath() && path.IsPrimPath())) {
        *errMsg = TfStringPrintf(
            "Path '%s' in '%s' must be an absolute path to a prim",
            clipPrimPath.c_str(),
            UsdClipsAPIInfoKeys->primPath.GetText());
        return false;
    }

    // Each Vec2d in the 'clipActive' array is a (start frame, clip index)
    // tuple. Ensure the clip index points to a valid clip.
    for (const auto& startFrameAndClipIndex : clipActive) {
        if (startFrameAndClipIndex[1] < 0 ||
            startFrameAndClipIndex[1] >= numClips) {

            *errMsg = TfStringPrintf(
                "Invalid clip index %d in '%s'", 
                (int)startFrameAndClipIndex[1],
                UsdClipsAPIInfoKeys->active.GetText());
            return false;
        }
    }

    // Ensure that 'clipActive' does not specify multiple clips to be
    // active at the same time.
    typedef std::map<double, int> _ActiveClipMap;
    _ActiveClipMap activeClipMap;
    for (const auto& startFrameAndClipIndex : clipActive) {
        std::pair<_ActiveClipMap::iterator, bool> status = 
            activeClipMap.insert(std::make_pair(
                    startFrameAndClipIndex[0], startFrameAndClipIndex[1]));
        
        if (!status.second) {
            *errMsg = TfStringPrintf(
                "Clip %d cannot be active at time %.3f in '%s' because "
                "clip %d was already specified as active at this time.",
                (int)startFrameAndClipIndex[1],
                startFrameAndClipIndex[0],
                UsdClipsAPIInfoKeys->active.GetText(),
                status.first->second);
            return false;
        }
    }

    // Ensure there are at most two (stage time, clip time) entries in
    // clip times that have the same stage time.
    if (clipTimes) {
        typedef std::unordered_map<double, int> _StageTimesMap;
        _StageTimesMap stageTimesMap;
        for (const auto& stageTimeAndClipTime : *clipTimes) {
            int& numSeen = stageTimesMap.emplace(
                stageTimeAndClipTime[0], 0).first->second;
            numSeen += 1;

            if (numSeen > 2) {
                *errMsg = TfStringPrintf(
                    "Cannot have more than two entries in '%s' with the same "
                    "stage time (%.3f).",
                    UsdClipsAPIInfoKeys->times.GetText(),
                    stageTimeAndClipTime[0]);
                return false;
            }
        }
    }

    return true;
}

// ------------------------------------------------------------

Usd_ClipSetRefPtr
Usd_ClipSet::New(
    const std::string& name,
    const Usd_ClipSetDefinition& clipDef,
    std::string* status)
{
    // If we haven't found all of the required clip metadata we can just 
    // bail out. Note that clipTimes and clipManifestAssetPath are *not* 
    // required.
    if (!clipDef.clipAssetPaths 
        || !clipDef.clipPrimPath 
        || !clipDef.clipActive) {
        return nullptr;
    }

    // XXX: Possibly want a better way to inform consumers of the error
    //      message..
    if (!_ValidateClipFields(
            *clipDef.clipAssetPaths, *clipDef.clipPrimPath, 
            *clipDef.clipActive,
            clipDef.clipTimes ? &(clipDef.clipTimes.value()) : nullptr,
            status)) {
        return nullptr;
    }

    // The clip manifest is currently optional but can greatly improve
    // performance if specified. For debugging performance problems,
    // issue a message indicating if one hasn't been specified.
    if (!clipDef.clipManifestAssetPath) {
        *status = "No clip manifest specified. "
            "Performance may be improved if a manifest is specified.";
    }

    return Usd_ClipSetRefPtr(new Usd_ClipSet(name, clipDef));
}

namespace
{
struct Usd_ClipEntry {
public:
    double startTime;
    SdfAssetPath clipAssetPath;
};
} // end anonymous namespace

Usd_ClipSet::Usd_ClipSet(
    const std::string& name_,
    const Usd_ClipSetDefinition& clipDef)
    : name(name_)
    , sourceLayerStack(clipDef.sourceLayerStack)
    , sourcePrimPath(clipDef.sourcePrimPath)
    , sourceLayer(
        clipDef.sourceLayerStack->GetLayers()[
            clipDef.indexOfLayerWhereAssetPathsFound])
    , clipPrimPath(SdfPath(*clipDef.clipPrimPath))
    , interpolateMissingClipValues(false)
{
    // NOTE: Assumes definition has already been validated

    // Generate a mapping of startTime -> clip entry. This allows us to
    // quickly determine the (startTime, endTime) for a given clip.
    typedef std::map<double, Usd_ClipEntry> _TimeToClipMap;
    _TimeToClipMap startTimeToClip;

    for (const auto& startFrameAndClipIndex : *clipDef.clipActive) {
        const double startFrame = startFrameAndClipIndex[0];
        const int clipIndex = (int)(startFrameAndClipIndex[1]);
        const SdfAssetPath& assetPath = (*clipDef.clipAssetPaths)[clipIndex];

        Usd_ClipEntry entry;
        entry.startTime = startFrame;
        entry.clipAssetPath = assetPath;

        // Validation should have caused us to bail out if there were any
        // conflicting clip activations set.
        TF_VERIFY(startTimeToClip.insert(
                std::make_pair(entry.startTime, entry)).second);
    }

    // Generate the clip time mapping that applies to all clips.
    std::shared_ptr<Usd_Clip::TimeMappings> times(new Usd_Clip::TimeMappings());
    if (clipDef.clipTimes) {
        for (const auto& clipTime : *clipDef.clipTimes) {
            const Usd_Clip::ExternalTime extTime = clipTime[0];
            const Usd_Clip::InternalTime intTime = clipTime[1];
            times->push_back(Usd_Clip::TimeMapping(extTime, intTime));
        }
    }

    if (!times->empty()) {
        // Maintain the relative order of entries with the same stage time for
        // jump discontinuities in case the authored times array was unsorted.
        std::stable_sort(times->begin(), times->end(),
                Usd_Clip::Usd_SortByExternalTime());

        // Jump discontinuities are represented by consecutive entries in the
        // times array with the same stage time, e.g. (10, 10), (10, 0).
        // We represent this internally as (10 - SafeStep(), 10), (10, 0)
        // because a lot of the desired behavior just falls out from this
        // representation.
        for (size_t i = 0; i < times->size() - 1; ++i) {
            if ((*times)[i].externalTime == (*times)[i + 1].externalTime) {
                (*times)[i].externalTime =
                    (*times)[i].externalTime - UsdTimeCode::SafeStep();
                (*times)[i].isJumpDiscontinuity = true;
            }
        }

        // Add sentinel values to the beginning and end for convenience.
        times->insert(times->begin(), times->front());
        times->insert(times->end(), times->back());
    }


    // Build up the final vector of clips.
    const _TimeToClipMap::const_iterator itBegin = startTimeToClip.begin();
    const _TimeToClipMap::const_iterator itEnd = startTimeToClip.end();

    _TimeToClipMap::const_iterator it = startTimeToClip.begin();
    while (it != itEnd) {
        const Usd_ClipEntry& clipEntry = it->second;

        const Usd_Clip::ExternalTime clipStartTime = 
            (it == itBegin ? Usd_ClipTimesEarliest : it->first);
        ++it;
        const Usd_Clip::ExternalTime clipEndTime = 
            (it == itEnd ? Usd_ClipTimesLatest : it->first);

        const Usd_ClipRefPtr clip(new Usd_Clip(
            /* clipSourceLayerStack = */ clipDef.sourceLayerStack,
            /* clipSourcePrimPath   = */ clipDef.sourcePrimPath,
            /* clipSourceLayerIndex = */ 
                clipDef.indexOfLayerWhereAssetPathsFound,
            /* clipAssetPath = */ clipEntry.clipAssetPath,
            /* clipPrimPath = */ clipPrimPath,
            /* clipAuthoredStartTime = */ clipEntry.startTime,
            /* clipStartTime = */ clipStartTime,
            /* clipEndTime = */ clipEndTime,
            /* clipTimes = */ times));

        valueClips.push_back(clip);
    }

    // Have the clipTimes saved on the ClipSet as well.
    _times = times;

    if (clipDef.interpolateMissingClipValues) {
        interpolateMissingClipValues = *clipDef.interpolateMissingClipValues;
    }

    // Create a clip for the manifest. If no manifest has been specified,
    // we generate one for the user automatically.
    SdfAssetPath manifestAssetPath;
    SdfLayerRefPtr generatedManifest;

    if (clipDef.clipManifestAssetPath) {
        manifestAssetPath = *clipDef.clipManifestAssetPath;
    }
    else {
        generatedManifest = Usd_GenerateClipManifest(
            valueClips, clipPrimPath, _tokens->generated_manifest.GetString());
        manifestAssetPath = SdfAssetPath(generatedManifest->GetIdentifier());
    }

    manifestClip.reset(new Usd_Clip(
        /* clipSourceLayerStack = */ clipDef.sourceLayerStack,
        /* clipSourcePrimPath   = */ clipDef.sourcePrimPath,
        /* clipSourceLayerIndex = */ clipDef.indexOfLayerWhereAssetPathsFound,
        /* clipAssetPath        = */ manifestAssetPath,
        /* clipPrimPath         = */ clipPrimPath,
        /* clipAuthoredStartTime= */ Usd_ClipTimesEarliest,
        /* clipStartTime        = */ Usd_ClipTimesEarliest,
        /* clipEndTime          = */ Usd_ClipTimesLatest,
        /* clipTimes            = */ std::make_shared<Usd_Clip::TimeMappings>()));

    if (generatedManifest) {
        // If we generated a manifest layer pull on the clip so that it takes
        // a reference to the generated layer and maintains ownership after
        // this function returns.
        TF_UNUSED(manifestClip->GetLayer());
    }
}

bool
Usd_ClipSet::_ClipContributesValue(
    const Usd_ClipRefPtr& clip, const SdfPath& path) const
{
    // If this clip interpolates values for clips without authored
    // values for an attribute, then we need to check whether the
    // clip actually contains authored values below. Otherwise every
    // clip contributes a value, so we can use the clip.
    if (!interpolateMissingClipValues) {
        return true;
    }

    // Use the clip if it has authored time samples for the attribute.
    // If this attribute is blocked at the clip's start time in the 
    // manifest it means the user has declared there are no samples
    // in that clip for this attribute. This allows us to skip
    // opening the layer to check if it has authored time samples.
    const bool hasAuthoredSamples = 
        !manifestClip->IsBlocked(path, clip->authoredStartTime) &&
        clip->HasAuthoredTimeSamples(path);
    if (hasAuthoredSamples) {
        return true;
    }

    // Use the clip if a default value specified in the manifest.
    if (Usd_HasDefault<int>(manifestClip, path, nullptr) 
            != Usd_DefaultValueResult::None) {
        return true;
    }

    return false;
}

bool
Usd_ClipSet::GetBracketingTimeSamplesForPath(
    const SdfPath& path, double time,
    double* lower, double* upper) const
{
    bool foundLower = false, foundUpper = false;

    const size_t clipIndex = _FindClipIndexForTime(time);

    const Usd_ClipRefPtr& activeClip = valueClips[clipIndex];
    if (_ClipContributesValue(activeClip, path)) {
        if (!TF_VERIFY(activeClip->GetBracketingTimeSamplesForPath(
                path, time, lower, upper))) {
            return false;
        }

        // Since each clip always has a time sample at its start time,
        // the above call will always establish the lower bracketing sample.
        foundLower = true;

        // If the given time is after the last time sample in the active
        // time range of the clip we need to search forward for the next 
        // clip that contributes a value and it to find the upper bracketing
        // sample. We indicate this by setting foundUpper to false.
        //
        // The diagram below shows an example, where the 'X's show time
        // samples, the '|' shows the endTime of clip 1 and start time
        // of clip 2, and 't' is the query time. The above call to
        // GetBracketingTimeSamples gets us the lower bound, which is the
        // time of the 'X' in clip 1.
        //         
        // Clip 1: ------X--t--- | 
        // Clip 2:               X ------------
        //
        foundUpper = !(*lower == *upper && time > *upper);
    }

    // If we haven't found the lower bracketing sample from the active
    // clip, search backwards to find the nearest clip that contributes
    // values and use that to determine the lower sample.
    for (size_t i = clipIndex; !foundLower && i-- != 0; ) {
        const Usd_ClipRefPtr& clip = valueClips[i];
        if (!_ClipContributesValue(clip, path)) {
            continue;
        }

        double tmpLower, tmpUpper;
        if (!TF_VERIFY(clip->GetBracketingTimeSamplesForPath(
                    path, time, &tmpLower, &tmpUpper))) {
            return false;
        }
            
        foundLower = true;
        *lower = tmpUpper;
    }

    // If we haven't found the upper bracketing sample from the active
    // clip, search forwards to find the nearest clip that contributes
    // values and use its start time as the upper sample. We can avoid
    // the cost of calling GetBracketingTimeSamples here since we know
    // a clip always has a time sample at its start frame if it
    // contributes values.
    for (size_t i = clipIndex + 1; !foundUpper && i < valueClips.size(); ++i) {
        const Usd_ClipRefPtr& clip = valueClips[i];
        if (!_ClipContributesValue(clip, path)) {
            continue;
        }

        *upper = clip->startTime;
        foundUpper = true;
    }

    // Reconcile foundLower and foundUpper values.
    if (foundLower && !foundUpper) {
        *upper = *lower;
    }
    else if (!foundLower && foundUpper) {
        *lower = *upper;
    }
    else if (!foundLower && !foundUpper) {
        // In this case, no clips have been found that contribute
        // values. Use the start time of the first clip as the sole
        // time sample.
        *lower = *upper = valueClips.front()->authoredStartTime;
    }

    return true;
}

bool
Usd_ClipSet::GetPreviousTimeSampleForPath(
    const SdfPath& path, double time, double* tPrevious) const
{
    const std::set<double> allTimeSamples = ListTimeSamplesForPath(path);
    if (allTimeSamples.empty()) {
        return false;
    }

    // Can't get a previous time sample if the given time is less than
    // or equal to the first time sample.
    if (time <= *allTimeSamples.begin()) {
        return false;
    }

    // Last time is the previous time if the query time is greater than
    // the last time sample.
    if (time > *allTimeSamples.rbegin()) {
        *tPrevious = *allTimeSamples.rbegin();
        return true;
    }

    // The previous time sample is the one before the lower_bound with the 
    // given time.
    auto it = allTimeSamples.lower_bound(time);

    // We can never be at the beginning of the set since we've already
    // checked that the given time is greater than the first time sample.
    TF_VERIFY(it != allTimeSamples.begin());

    *tPrevious = *--it;
    return true;
}

bool
Usd_ClipSet::_HasJumpDiscontinuityAtTime(double time) const
{
    if (_times->empty()) {
        return false;
    }
    
    const auto it = std::lower_bound(
        _times->begin(), _times->end(), time, 
        Usd_Clip::Usd_SortByExternalTime());

    // - time is within the range of the clip times, and
    // - time is same as the external time of the found clip time, and
    // - Since jump discontinuities are represented on the previous mapping
    // entry, we need to check if the previous entry is a jump
    // discontinuity (making sure we are not at the beginning of the time
    // mappings).
    return (it != _times->end() && (it->externalTime == time) && 
            it != _times->begin() && (it-1)->isJumpDiscontinuity);
}

std::set<double>
Usd_ClipSet::ListTimeSamplesForPath(const SdfPath& path) const
{
    std::set<double> samples;
    for (const Usd_ClipRefPtr& clip : valueClips) {
        if (!_ClipContributesValue(clip, path)) {
            continue;
        }

        const std::set<Usd_Clip::ExternalTime> clipSamples =
            clip->ListTimeSamplesForPath(path);
        samples.insert(clipSamples.begin(), clipSamples.end());
    }

    if (samples.empty()) {
        // In this case, no clips have been found that contribute
        // values. Use the start time of the first clip as the sole
        // time sample.
        samples.insert(valueClips.front()->authoredStartTime);
    }

    return samples;
}

std::vector<double>
Usd_ClipSet::GetTimeSamplesInInterval(
    const SdfPath& path, const GfInterval& interval) const
{
    std::vector<double> timeSamples;

    for (const Usd_ClipRefPtr& clip : valueClips) {
        if (interval.IsMaxOpen() ? 
            clip->startTime >= interval.GetMax() :
            clip->startTime > interval.GetMax()) {
            // Clips are ordered by increasing start time. Once we hit a clip
            // whose start time is greater than the given interval, we can stop
            // looking.
            break;
        }

        if (!interval.Intersects(GfInterval(
                clip->startTime, clip->endTime, 
                /* minClosed = */ true, /* maxClosed = */ false))) {
            continue;
        }

        if (!_ClipContributesValue(clip, path)) {
            continue;
        }
        
        Usd_CopyTimeSamplesInInterval(
            clip->ListTimeSamplesForPath(path), interval, &timeSamples);
    }

    // If we haven't found any time samples in the interval, we need to check
    // whether there are any clips that provide samples. If there are none,
    // we always add the start time of the first clip as the sole time sample.
    // See ListTimeSamplesForPath.
    //
    // Note that in the common case where interpolation of missing values is
    // disabled, all clips contribute time samples.
    if (timeSamples.empty() &&
        std::none_of(
            valueClips.begin(), valueClips.end(), 
            [this, &path](const Usd_ClipRefPtr& clip) {
                return _ClipContributesValue(clip, path);
            })) {

        const Usd_ClipRefPtr& firstClip = valueClips.front();
        if (interval.Contains(firstClip->authoredStartTime)) {
            timeSamples.push_back(firstClip->authoredStartTime);
        }
    }

    return timeSamples;
}

size_t
Usd_ClipSet::_FindClipIndexForTime(double time) const
{
    size_t clipIndex = 0;

    // If there was only one clip, it must be active over all time so
    // we don't need to search any further.
    if (valueClips.size() > 1) {
        // Find the first clip whose start time is greater than the given
        // time.
        auto it = std::upper_bound(
            valueClips.begin(), valueClips.end(), time,
            [](double time, const Usd_ClipRefPtr& clip) {
                return time < clip->startTime;
            });

        // The upper bound should never be the first clip. Since its
        // startTime is set to -inf, it should never be greater than any
        // time that is given.
        if (TF_VERIFY(it != valueClips.begin())) {
            // The clip before the upper bound must have time in its
            // [startTime, endTime) range. This is true even if
            // it points to valueClips.end(); if this were the case,
            // then the given time is greater than the last clip's
            // startTime, but the last clip is active until +inf so
            // time must fall into its range.
            clipIndex = std::distance(valueClips.begin(), it) - 1;
        }
    }

    return TF_VERIFY(
        clipIndex < valueClips.size()
        && time >= valueClips[clipIndex]->startTime 
        && time < valueClips[clipIndex]->endTime) ? clipIndex : 0;
}

PXR_NAMESPACE_CLOSE_SCOPE
