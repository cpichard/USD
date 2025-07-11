//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hgiVulkan/conversions.h"
#include "pxr/imaging/hgiVulkan/device.h"
#include "pxr/imaging/hgiVulkan/diagnostic.h"
#include "pxr/imaging/hgiVulkan/garbageCollector.h"
#include "pxr/imaging/hgiVulkan/hgi.h"
#include "pxr/imaging/hgiVulkan/shaderCompiler.h"
#include "pxr/imaging/hgiVulkan/shaderFunction.h"
#include "pxr/imaging/hgiVulkan/shaderGenerator.h"

#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE


HgiVulkanShaderFunction::HgiVulkanShaderFunction(
    HgiVulkanDevice* device,
    Hgi const* hgi,
    HgiShaderFunctionDesc const& desc,
    int shaderVersion)
    : HgiShaderFunction(desc)
    , _device(device)
    , _spirvByteSize(0)
    , _vkShaderModule(nullptr)
    , _inflightBits(0)
{
    VkShaderModuleCreateInfo shaderCreateInfo =
        {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    std::vector<unsigned int> spirv;

    const char* debugLbl = _descriptor.debugName.empty() ?
        "unknown" : _descriptor.debugName.c_str();

    HgiVulkanShaderGenerator shaderGenerator(hgi, desc);
    shaderGenerator.Execute();
    const char *shaderCode = shaderGenerator.GetGeneratedShaderCode();

    // Compile shader and capture errors
    bool result = HgiVulkanCompileGLSL(
        debugLbl,
        &shaderCode,
        1,
        desc.shaderStage,
        &spirv,
        &_errors);

    // Create vulkan module if there were no errors.
    if (result) {
        _spirvByteSize = spirv.size() * sizeof(unsigned int);

        shaderCreateInfo.codeSize = _spirvByteSize;
        shaderCreateInfo.pCode = (uint32_t*) spirv.data();

        HGIVULKAN_VERIFY_VK_RESULT(
            vkCreateShaderModule(
                device->GetVulkanDevice(),
                &shaderCreateInfo,
                HgiVulkanAllocator(),
                &_vkShaderModule)
        );

        // Debug label
        if (!_descriptor.debugName.empty()) {
            std::string debugLabel = "ShaderModule " + _descriptor.debugName;
            HgiVulkanSetDebugName(
                device,
                (uint64_t)_vkShaderModule,
                VK_OBJECT_TYPE_SHADER_MODULE,
                debugLabel.c_str());
        }

        // Get descriptor set info.
        _descriptorSetInfo = shaderGenerator.GetDescriptorSetInfo();
    }

    // Clear these pointers in our copy of the descriptor since we
    // have to assume they could become invalid after we return.
    _descriptor.shaderCodeDeclarations = nullptr;
    _descriptor.shaderCode = nullptr;
    _descriptor.generatedShaderCodeOut = nullptr;
}

HgiVulkanShaderFunction::~HgiVulkanShaderFunction()
{
    if (_vkShaderModule) {
        vkDestroyShaderModule(
            _device->GetVulkanDevice(),
            _vkShaderModule,
            HgiVulkanAllocator());
    }
}

VkShaderStageFlagBits
HgiVulkanShaderFunction::GetShaderStage() const
{
    return VkShaderStageFlagBits(
        HgiVulkanConversions::GetShaderStages(_descriptor.shaderStage));
}

VkShaderModule
HgiVulkanShaderFunction::GetShaderModule() const
{
    return _vkShaderModule;
}

const char*
HgiVulkanShaderFunction::GetShaderFunctionName() const
{
    static const std::string entry("main");
    return entry.c_str();
}

bool
HgiVulkanShaderFunction::IsValid() const
{
    return _errors.empty();
}

std::string const&
HgiVulkanShaderFunction::GetCompileErrors()
{
    return _errors;
}

size_t
HgiVulkanShaderFunction::GetByteSizeOfResource() const
{
    return _spirvByteSize;
}

uint64_t
HgiVulkanShaderFunction::GetRawResource() const
{
    return (uint64_t) _vkShaderModule;
}

HgiVulkanDescriptorSetInfoVector const&
HgiVulkanShaderFunction::GetDescriptorSetInfo() const
{
    return _descriptorSetInfo;
}

HgiVulkanDevice*
HgiVulkanShaderFunction::GetDevice() const
{
    return _device;
}

uint64_t &
HgiVulkanShaderFunction::GetInflightBits()
{
    return _inflightBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
