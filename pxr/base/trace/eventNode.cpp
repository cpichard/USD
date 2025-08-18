//
// Copyright 2018 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/trace/eventNode.h"

#include "pxr/pxr.h"

PXR_NAMESPACE_OPEN_SCOPE

TraceEventNodeRefPtr
TraceEventNode::Append(
    const TfToken &key, 
    TraceCategoryId category, 
    TimeStamp beginTime, 
    TimeStamp endTime,
    bool separateEvents)
{
    TraceEventNodeRefPtr n = 
        TraceEventNode::New(
            key, category, beginTime, endTime, {}, separateEvents);
    Append(std::move(n)); 
    return n;
}

void
TraceEventNode::Append(TraceEventNodeRefPtr node)
{
    _children.emplace_back(std::move(node));
}

void 
TraceEventNode::SetBeginAndEndTimesFromChildren()
{
    if (_children.empty()) {
        _beginTime = 0;
        _endTime = 0;
        return;
    }

    _beginTime = std::numeric_limits<TimeStamp>::max();
    _endTime   = std::numeric_limits<TimeStamp>::min();

    for (const TraceEventNodeRefPtr& c : _children) {
        _beginTime = std::min(_beginTime, c->GetBeginTime());
        _endTime   = std::max(_endTime, c->GetEndTime());
    }
}

const TraceEventNode::AttributeMap&
TraceEventNode::GetAttributes() const
{
    static const AttributeMap empty;
    return _attributesAndSeparateEvents.Get() ? *_attributesAndSeparateEvents: empty;
}

void
TraceEventNode::AddAttribute(
    const TfToken& key, const AttributeData& attr)
{
    if (!_attributesAndSeparateEvents.Get()) {
        _attributesAndSeparateEvents.Set(new AttributeMap);
    }
    _attributesAndSeparateEvents->emplace(key, attr);
}

PXR_NAMESPACE_CLOSE_SCOPE
