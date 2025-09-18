//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"

#include "pxr/imaging/hgiVulkan/capabilities.h"

#include "pxr/imaging/hgiVulkan/device.h"
#include "pxr/imaging/hgiVulkan/diagnostic.h"
#include "pxr/imaging/hgiVulkan/debugCodes.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HGIVULKAN_ENABLE_MULTI_DRAW_INDIRECT, true,
                      "Use Vulkan multi draw indirect");
TF_DEFINE_ENV_SETTING(HGIVULKAN_ENABLE_BUILTIN_BARYCENTRICS, false,
                      "Use Vulkan built in barycentric coordinates");
TF_DEFINE_ENV_SETTING(HGIVULKAN_ENABLE_NATIVE_INTEROP, true,
                      "Enable native interop with OpenGL (if device supports)");
TF_DEFINE_ENV_SETTING(HGIVULKAN_ENABLE_UMA, true,
                      "Use Vulkan with UMA (if device supports)");
TF_DEFINE_ENV_SETTING(HGIVULKAN_ENABLE_REBAR, false,
                      "Use Vulkan with ReBAR (if device supports)");

static void _DumpDeviceDeviceMemoryProperties(
    const VkPhysicalDeviceMemoryProperties& vkMemoryProperties)
{
    std::cout << "Vulkan memory info:\n";
    for (uint32_t heapIndex = 0;
            heapIndex < vkMemoryProperties.memoryHeapCount; heapIndex++) {
        std::cout << "Heap " << heapIndex << ":\n";
        const auto& heap = vkMemoryProperties.memoryHeaps[heapIndex];
        std::cout << "    Size: " << heap.size << "\n";
        std::cout << "    Flags:";
        if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            std::cout << " DEVICE_LOCAL";
        }
        if (heap.flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) {
            std::cout << " MULTI_INSTANCE";
        }
        std::cout << "\n";

        for (uint32_t typeIndex = 0;
                typeIndex < vkMemoryProperties.memoryTypeCount; typeIndex++) {
            const auto& memoryType = vkMemoryProperties.memoryTypes[typeIndex];
            if (memoryType.heapIndex != heapIndex) {
                continue;
            }

            std::cout << "    Memory type " << typeIndex << ":\n";
            std::cout << "        Flags:";
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                std::cout << " DEVICE_LOCAL";
            }
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                std::cout << " HOST_VISIBLE";
            }
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
                std::cout << " HOST_COHERENT";
            }
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
                std::cout << " HOST_CACHED";
            }
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
                std::cout << " LAZILY_ALLOCATED";
            }
            if (memoryType.propertyFlags &
                    VK_MEMORY_PROPERTY_PROTECTED_BIT) {
                std::cout << " PROTECTED";
            }
            std::cout << "\n";
        }
    }
    std::cout << std::flush;
}

// Returns true if all device memory can be accessed from the host directly,
// as-if it was local to the host. This is typically made available by UMA
// (unified memory architecture) or ReBAR (resizable base address register).
// This should be true for integrated GPUs, dedicated GPUs on systems with ReBAR
// enabled, and software renderers (like Lavapipe).
static bool
_SupportsHostAccessibleDeviceMemory(
    const VkPhysicalDeviceMemoryProperties& memoryProperties)
{
    for (uint32_t heapIndex = 0;
        heapIndex < memoryProperties.memoryHeapCount; heapIndex++) {
        const auto& heap = memoryProperties.memoryHeaps[heapIndex];

        // ReBAR has a more basic predecessor called simply BAR. It's limited
        // to only 256MiB, but otherwise has the exact same flags. While it has
        // its uses for small resources that change often like uniforms, it
        // would be much more difficult to use with Hgi, so we'll ignore it.
        static constexpr size_t barMaxSize = 256 * 1024 * 1024;
        if (!(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ||
            heap.size <= barMaxSize) {
            continue;
        }

         for (uint32_t typeIndex = 0;
                typeIndex < memoryProperties.memoryTypeCount; typeIndex++) {
            const auto& memoryType = memoryProperties.memoryTypes[typeIndex];
            if (memoryType.heapIndex != heapIndex) {
                continue;
            }

            // We're looking for a heap that's on the device, but is host
            // visible. We also want host coherence so writes are automatically
            // visible and available on the device. Heaps with these properties
            // show up on UMA and ReBAR enabled GPUs. See:
            // https://asawicki.info/news_1740_vulkan_memory_types_on_pc_and_how_to_use_them
            // We don't strictly need HOST_COHERENT_BIT, but it'll make things
            // simpler. It's really only on less recent mobile devices that
            // it's not available (mostly before 2021).
            static constexpr auto deviceLocalHostAccessibleFlags =
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
             if ((memoryType.propertyFlags & deviceLocalHostAccessibleFlags) ==
                deviceLocalHostAccessibleFlags) {
                return true;
            }
        }
    }

    return false;
}

HgiVulkanCapabilities::HgiVulkanCapabilities(HgiVulkanDevice* device)
    : supportsTimeStamps(false),
    supportsNativeInterop(false)
{
    VkPhysicalDevice physicalDevice = device->GetVulkanPhysicalDevice();

    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, 0);
    std::vector<VkQueueFamilyProperties> queues(queueCount);

    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice,
        &queueCount,
        queues.data());

    // Grab the properties of all queues up until the (gfx) queue we are using.
    uint32_t gfxQueueIndex = device->GetGfxQueueFamilyIndex();

    // The last queue we grabbed the properties of is our gfx queue.
    if (TF_VERIFY(gfxQueueIndex < queues.size())) {
        VkQueueFamilyProperties const& gfxQueue = queues[gfxQueueIndex];
        supportsTimeStamps = gfxQueue.timestampValidBits > 0;
    }

    //
    // Physical device properties
    //
    vkDeviceProperties2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    if (TfDebug::IsEnabled(HGIVULKAN_DUMP_DEVICE_MEMORY_PROPERTIES)) {
        _DumpDeviceDeviceMemoryProperties(vkMemoryProperties);
    }

    // Vertex attribute divisor properties ext
    vkVertexAttributeDivisorProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT;
    vkDeviceProperties2.pNext = &vkVertexAttributeDivisorProperties;

    vkPhysicalDeviceIdProperties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR;
    vkPhysicalDeviceIdProperties.pNext = vkDeviceProperties2.pNext;
    vkDeviceProperties2.pNext =  &vkPhysicalDeviceIdProperties;

    // Query device properties
    vkGetPhysicalDeviceProperties2(physicalDevice, &vkDeviceProperties2);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &vkMemoryProperties);

    //
    // Physical device features
    //
    vkDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // Vulkan 1.1 features
    vkVulkan11Features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vkDeviceFeatures2.pNext = &vkVulkan11Features;

    // Interop features
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (TfGetEnvSetting(HGIVULKAN_ENABLE_NATIVE_INTEROP) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME)) {
        supportsNativeInterop = true;
    }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    if (TfGetEnvSetting(HGIVULKAN_ENABLE_NATIVE_INTEROP) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
        device->IsSupportedExtension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
        supportsNativeInterop = true;
    }
#elif defined(VK_USE_PLATFORM_METAL_EXT)
    // To be added, either through MoltenVK adding GL interop,
    // or a later change if necessary
#endif

    // Vertex attribute divisor features ext
    vkVertexAttributeDivisorFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
    vkVertexAttributeDivisorFeatures.pNext = vkDeviceFeatures2.pNext;
    vkDeviceFeatures2.pNext = &vkVertexAttributeDivisorFeatures;

    // Barycentric features
    const bool barycentricExtSupported = device->IsSupportedExtension(
        VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
    if (barycentricExtSupported) {
        vkBarycentricFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR;
        vkBarycentricFeatures.pNext = vkDeviceFeatures2.pNext;
        vkDeviceFeatures2.pNext =  &vkBarycentricFeatures;
    }
    
    // Line rasterization features
    const bool lineRasterizationExtSupported = device->IsSupportedExtension(
        VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME);
    if (lineRasterizationExtSupported) {
        vkLineRasterizationFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_KHR;
        vkLineRasterizationFeatures.pNext = vkDeviceFeatures2.pNext;
        vkDeviceFeatures2.pNext =  &vkLineRasterizationFeatures;
    }

    // Query device features
    vkGetPhysicalDeviceFeatures2(physicalDevice, &vkDeviceFeatures2);

    // Verify we meet feature and extension requirements

    // Storm with HgiVulkan needs gl_BaseInstance/gl_BaseInstanceARB in shader.
    TF_VERIFY(
        vkVulkan11Features.shaderDrawParameters);

    TF_VERIFY(
        vkVertexAttributeDivisorFeatures.vertexAttributeInstanceRateDivisor);

    const bool hostAccessibleDeviceMemory =
        _SupportsHostAccessibleDeviceMemory(vkMemoryProperties);
    // If the device is located on or near the host, then it's probably UMA,
    // anything else is probably ReBAR.
    const bool uma = hostAccessibleDeviceMemory &&
        (vkDeviceProperties2.properties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
        vkDeviceProperties2.properties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_CPU);
    const bool rebar = hostAccessibleDeviceMemory && !uma;
    // For simplicity we'll treat UMA and ReBAR as the same using the
    // HgiDeviceCapabilitiesBitsUnifiedMemory flag.
    const bool unifiedMemory =
        (uma && TfGetEnvSetting(HGIVULKAN_ENABLE_UMA)) ||
        (rebar && TfGetEnvSetting(HGIVULKAN_ENABLE_REBAR));

    if (HgiVulkanIsDebugEnabled()) {
        auto memoryAccessString = "";
        if (unifiedMemory) {
            memoryAccessString = rebar ? " (ReBAR)" : " (UMA)";
        }
        TF_STATUS("Selected GPU: \"%s\"%s",
            vkDeviceProperties2.properties.deviceName, memoryAccessString);
    }

    _maxClipDistances = vkDeviceProperties2.properties.limits.maxClipDistances;
    _maxUniformBlockSize =
        vkDeviceProperties2.properties.limits.maxUniformBufferRange;
    _maxShaderStorageBlockSize =
        vkDeviceProperties2.properties.limits.maxStorageBufferRange;
    _uniformBufferOffsetAlignment =
        vkDeviceProperties2.properties.limits.minUniformBufferOffsetAlignment;

    const bool conservativeRasterEnabled = device->IsSupportedExtension(
        VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
    const bool shaderDrawParametersEnabled =
        vkVulkan11Features.shaderDrawParameters;
    bool multiDrawIndirectEnabled = true;
    bool builtinBarycentricsEnabled =
        barycentricExtSupported &&
        vkBarycentricFeatures.fragmentShaderBarycentric;

    // Check Hgi env settings
    if (!TfGetEnvSetting(HGIVULKAN_ENABLE_MULTI_DRAW_INDIRECT)) {
        multiDrawIndirectEnabled = false;
    }
    if (!TfGetEnvSetting(HGIVULKAN_ENABLE_BUILTIN_BARYCENTRICS)) {
        builtinBarycentricsEnabled = false;
    }

    _SetFlag(HgiDeviceCapabilitiesBitsUnifiedMemory, unifiedMemory);
    _SetFlag(HgiDeviceCapabilitiesBitsDepthRangeMinusOnetoOne, false);
    _SetFlag(HgiDeviceCapabilitiesBitsStencilReadback, true);
    _SetFlag(HgiDeviceCapabilitiesBitsShaderDoublePrecision, true);
    _SetFlag(HgiDeviceCapabilitiesBitsSingleSlotResourceArrays, true);
    _SetFlag(HgiDeviceCapabilitiesBitsConservativeRaster, 
        conservativeRasterEnabled);
    _SetFlag(HgiDeviceCapabilitiesBitsBuiltinBarycentrics, 
        builtinBarycentricsEnabled);
    _SetFlag(HgiDeviceCapabilitiesBitsShaderDrawParameters, 
        shaderDrawParametersEnabled);
     _SetFlag(HgiDeviceCapabilitiesBitsMultiDrawIndirect,
        multiDrawIndirectEnabled);
}

HgiVulkanCapabilities::~HgiVulkanCapabilities() = default;

int
HgiVulkanCapabilities::GetAPIVersion() const
{
    return static_cast<int>(vkDeviceProperties2.properties.apiVersion);
}

int
HgiVulkanCapabilities::GetShaderVersion() const
{
    // Note: This is not the Vulkan Shader Language version. It is provided for
    // compatibility with code that is asking for the GLSL version.
    return 450;
}

PXR_NAMESPACE_CLOSE_SCOPE
