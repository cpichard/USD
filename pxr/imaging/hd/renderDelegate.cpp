//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE


TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<HdRenderDelegate>();
}

//
// WORKAROUND: As these classes are pure interface classes, they do not need a
// vtable.  However, it is possible that some users will use rtti.
// This will cause a problem for some of our compilers:
//
// In particular clang will throw a warning: -wweak-vtables
// For gcc, there is an issue were the rtti typeid's are different.
//
// As destruction of the class is not on the performance path,
// the body of the deleter is provided here, so a vtable is created
// in this compilation unit.
HdRenderParam::~HdRenderParam() = default;
HdRenderDelegate::~HdRenderDelegate() = default;

HdRenderDelegate::HdRenderDelegate()
    : _settingsVersion(1)
{
}

bool
HdRenderParam::SetArbitraryValue(const TfToken& key, const VtValue& value)
{
    // Default implementation does not allow setting any arbitrary values.
    return false;
}

VtValue
HdRenderParam::GetArbitraryValue(const TfToken& key) const
{
    // Default implementation does not provide any arbitrary values.
    return VtValue();
}

bool
HdRenderParam::HasArbitraryValue(const TfToken& key) const
{
    // Default implementation does not provide any arbitrary values.
    return false;
}

bool
HdRenderParam::IsValid() const
{
    return false;
}

HdRenderDelegate::HdRenderDelegate(HdRenderSettingsMap const& settingsMap)
    : _settingsMap(settingsMap), _settingsVersion(1)
{
    if (TfDebug::IsEnabled(HD_RENDER_SETTINGS)) {
        std::cout << "Initial Render Settings" << std::endl;
        for (auto const& pair : _settingsMap) {
            std::cout << "\t[" << pair.first << "] = " << pair.second
                      << std::endl;
        }
    }
}

void
HdRenderDelegate::SetDrivers(HdDriverVector const& drivers)
{
}

HdRenderPassStateSharedPtr
HdRenderDelegate::CreateRenderPassState() const
{
    return std::make_shared<HdRenderPassState>();
}

TfToken
HdRenderDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->preview;
}

TfTokenVector 
HdRenderDelegate::GetShaderSourceTypes() const
{
    return TfTokenVector();
}

// deprecated
TfToken 
HdRenderDelegate::GetMaterialNetworkSelector() const
{
    return TfToken();
}

TfTokenVector
HdRenderDelegate::GetMaterialRenderContexts() const
{
    // To support RenderDelegates that have not yet updated 
    // GetMaterialNetworkSelector()
    return {GetMaterialNetworkSelector()};
}

TfTokenVector
HdRenderDelegate::GetRenderSettingsNamespaces() const
{
    return TfTokenVector();
}


bool
HdRenderDelegate::IsPrimvarFilteringNeeded() const
{
    return false;
}

HdAovDescriptor
HdRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    return HdAovDescriptor();
}

HdRenderSettingDescriptorList
HdRenderDelegate::GetRenderSettingDescriptors() const
{
    return HdRenderSettingDescriptorList();
}

void
HdRenderDelegate::SetRenderSetting(TfToken const& key, VtValue const& value)
{
    auto iter = _settingsMap.find(key);
    if (iter == _settingsMap.end()) {
        _settingsMap[key] = value;
        ++_settingsVersion;
    } else if (iter->second != value) {
        iter->second = value;
        ++_settingsVersion;
    }
    
    if (TfDebug::IsEnabled(HD_RENDER_SETTINGS)) {
        std::cout << "Render Setting [" << key << "] = " << value << std::endl;
    }
}

VtValue
HdRenderDelegate::GetRenderSetting(TfToken const& key) const
{
    auto it = _settingsMap.find(key);
    if (it != _settingsMap.end()) {
        return it->second;
    }

    if (TfDebug::IsEnabled(HD_RENDER_SETTINGS)) {
        std::cout << "Render setting not found for key [" << key << "]"
                  << std::endl;
    }
    return VtValue();
}

unsigned int
HdRenderDelegate::GetRenderSettingsVersion() const
{
    return _settingsVersion;
}

HdCommandDescriptors 
HdRenderDelegate::GetCommandDescriptors() const
{
    return HdCommandDescriptors();
}

bool 
HdRenderDelegate::InvokeCommand(
    const TfToken &command,
    const HdCommandArgs &args)
{
    // Fail all commands that get here.
    return false;
}

VtDictionary 
HdRenderDelegate::GetRenderStats() const
{
    return VtDictionary();
}

HdContainerDataSourceHandle
HdRenderDelegate::GetCapabilities() const
{
    return nullptr;
}

void
HdRenderDelegate::_PopulateDefaultSettings(
    HdRenderSettingDescriptorList const& defaultSettings)
{
    for (size_t i = 0; i < defaultSettings.size(); ++i) {
        if (_settingsMap.count(defaultSettings[i].key) == 0) {
            _settingsMap[defaultSettings[i].key] =
                defaultSettings[i].defaultValue;
        }
    }
}

HdRenderParam *
HdRenderDelegate::GetRenderParam() const 
{
    return nullptr;
}

bool
HdRenderDelegate::IsPauseSupported() const
{
    return false;
}

bool
HdRenderDelegate::IsPaused() const
{
    return false;
}

bool
HdRenderDelegate::Pause()
{
    return false;
}

bool HdRenderDelegate::IsParallelSyncEnabled(
    const TfToken &primType) const
{
    if (primType == HdPrimTypeTokens->extComputation) {
        return true;
    }
    return false;
}

bool
HdRenderDelegate::Resume()
{
    return false;
}

bool
HdRenderDelegate::IsStopSupported() const
{
    return false;
}

bool
HdRenderDelegate::IsStopped() const
{
    return true;
}

bool
HdRenderDelegate::Stop(bool blocking)
{
    return true;
}

bool
HdRenderDelegate::Restart()
{
    return false;
}

////////////////////////////////////////////////////////////////////////////
///
/// Hydra 2.0 API
///
////////////////////////////////////////////////////////////////////////////

void
HdRenderDelegate::SetTerminalSceneIndex(
    const HdSceneIndexBaseRefPtr &terminalSceneIndex)
{
}

void
HdRenderDelegate::Update()
{
}

PXR_NAMESPACE_CLOSE_SCOPE

