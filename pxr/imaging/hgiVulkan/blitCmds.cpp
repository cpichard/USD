//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hgiVulkan/blitCmds.h"
#include "pxr/imaging/hgiVulkan/buffer.h"
#include "pxr/imaging/hgiVulkan/commandBuffer.h"
#include "pxr/imaging/hgiVulkan/commandQueue.h"
#include "pxr/imaging/hgiVulkan/conversions.h"
#include "pxr/imaging/hgiVulkan/device.h"
#include "pxr/imaging/hgiVulkan/diagnostic.h"
#include "pxr/imaging/hgiVulkan/hgi.h"
#include "pxr/imaging/hgiVulkan/texture.h"
#include "pxr/imaging/hgi/blitCmdsOps.h"


PXR_NAMESPACE_OPEN_SCOPE

HgiVulkanBlitCmds::HgiVulkanBlitCmds(HgiVulkan* hgi)
    : _hgi(hgi)
    , _commandBuffer(nullptr)
{
    // We do not acquire the command buffer here, because the Cmds object may
    // have been created on the main thread, but used on a secondary thread.
    // We need to acquire a command buffer for the thread that is doing the
    // recording so we postpone acquiring cmd buffer until first use of Cmds.
}

HgiVulkanBlitCmds::~HgiVulkanBlitCmds() = default;

void
HgiVulkanBlitCmds::PushDebugGroup(const char* label)
{
    _CreateCommandBuffer();
    HgiVulkanBeginLabel(_hgi->GetPrimaryDevice(), _commandBuffer, label);
}

void
HgiVulkanBlitCmds::PopDebugGroup()
{
    _CreateCommandBuffer();
    HgiVulkanEndLabel(_hgi->GetPrimaryDevice(), _commandBuffer);
}

static
VkImageAspectFlags
_GetImageAspectMaskForCopy(HgiTextureUsage textureUsage)
{
    // XXX: Vulkan validation demands that only one flag at a time be used 
    // during the copy operation. Both depth and stencil flags cannot be 
    // simultaneously passed as aspects to copy.
    // So, we assume that the user wants to copy depth when the texture is a 
    // depthStencil texture. If need arises, this part of the implementation 
    // needs to be re-written such that an aspect flag is passed to this copy 
    // operation to resolve the discrepancy.
    VkImageAspectFlags aspectFlags =
        HgiVulkanConversions::GetImageAspectFlag(textureUsage);
    if (aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return aspectFlags;
}

void
HgiVulkanBlitCmds::CopyTextureGpuToCpu(
    HgiTextureGpuToCpuOp const& copyOp)
{
    _CreateCommandBuffer();

    HgiVulkanTexture* srcTexture =
        static_cast<HgiVulkanTexture*>(copyOp.gpuSourceTexture.Get());

    if (!TF_VERIFY(srcTexture && srcTexture->GetImage(),
        "Invalid texture handle")) {
        return;
    }

    if (copyOp.destinationBufferByteSize == 0) {
        TF_WARN("The size of the data to copy was zero (aborted)");
        return;
    }

    HgiTextureDesc const& texDesc = srcTexture->GetDescriptor();

    bool isTexArray = texDesc.layerCount>1;
    int depthOffset = isTexArray ? 0 : copyOp.sourceTexelOffset[2];

    VkOffset3D origin;
    origin.x = copyOp.sourceTexelOffset[0];
    origin.y = copyOp.sourceTexelOffset[1];
    origin.z = depthOffset;

    VkExtent3D size;
    size.width = texDesc.dimensions[0] - copyOp.sourceTexelOffset[0];
    size.height = texDesc.dimensions[1] - copyOp.sourceTexelOffset[1];
    size.depth = texDesc.dimensions[2] - depthOffset;

    VkImageSubresourceLayers imageSub;
    imageSub.baseArrayLayer = isTexArray ? copyOp.sourceTexelOffset[2] : 0;
    imageSub.layerCount = 1;
    imageSub.mipLevel = copyOp.mipLevel;
    imageSub.aspectMask = _GetImageAspectMaskForCopy(texDesc.usage);

    // See vulkan docs: Copying Data Between Buffers and Images
    VkBufferImageCopy region;
    region.bufferImageHeight = 0; // Buffer is tightly packed, like image
    region.bufferRowLength = 0;   // Buffer is tightly packed, like image
    region.bufferOffset = 0;      // We offset cpuDestinationBuffer. Not here.
    region.imageExtent = size;
    region.imageOffset = origin;
    region.imageSubresource = imageSub;

    // Transition image to TRANSFER_READ
    VkImageLayout oldLayout = srcTexture->GetImageLayout();
    srcTexture->TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        oldLayout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES,  // no pending writes
        VK_ACCESS_TRANSFER_READ_BIT,          // type of access
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    // producer stage
        VK_PIPELINE_STAGE_TRANSFER_BIT);      // consumer stage

    // Copy gpu texture to gpu staging buffer.
    // We reuse the texture's staging buffer, assuming that any new texel
    // uploads this frame will have been consumed from the staging buffer
    // before any downloads (read backs) overwrite the staging buffer texels.
    uint8_t* src = static_cast<uint8_t*>(srcTexture->GetCPUStagingAddress());
    HgiVulkanBuffer* stagingBuffer = srcTexture->GetStagingBuffer();
    TF_VERIFY(src && stagingBuffer);

    vkCmdCopyImageToBuffer(
        _commandBuffer->GetVulkanCommandBuffer(),
        srcTexture->GetImage(),
        srcTexture->GetImageLayout(),
        stagingBuffer->GetVulkanBuffer(),
        1,
        &region);

    // Transition image back to what it was.
    VkAccessFlags access = HgiVulkanTexture::GetDefaultAccessFlags(
        srcTexture->GetDescriptor().usage);

    srcTexture->TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        oldLayout,                           // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES, // no pending writes
        access,                              // type of access
        VK_PIPELINE_STAGE_TRANSFER_BIT,      // producer stage
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT); // consumer stage

    // Offset into the dst buffer
    char* dst = ((char*) copyOp.cpuDestinationBuffer) +
        copyOp.destinationByteOffset;

    // bytes to copy
    size_t byteSize = copyOp.destinationBufferByteSize;

    // Copy to cpu buffer when cmd buffer has been executed
    _commandBuffer->AddCompletedHandler(
        [dst, src, byteSize]{ memcpy(dst, src, byteSize);}
    );
}

void
HgiVulkanBlitCmds::CopyTextureCpuToGpu(
    HgiTextureCpuToGpuOp const& copyOp)
{
    _CreateCommandBuffer();

    HgiVulkanTexture* dstTexture = static_cast<HgiVulkanTexture*>(
        copyOp.gpuDestinationTexture.Get());
    HgiTextureDesc const& texDesc = dstTexture->GetDescriptor();

    // If we used GetCPUStagingAddress as the cpuSourceBuffer when the copyOp
    // was created, we can skip the memcpy since the src and dst buffer are
    // the same and dst staging buffer already contains the desired data.
    // See also: HgiVulkanTexture::GetCPUStagingAddress.
    if (!dstTexture->IsCPUStagingAddress(copyOp.cpuSourceBuffer)) {

        // We need to memcpy at the mip's location in the staging buffer.
        // It is possible we CopyTextureCpuToGpu a bunch of mips in a row
        // before submitting the cmd buf. So we can't just use the start
        // of the staging buffer each time.
        const std::vector<HgiMipInfo> mipInfos =
            HgiGetMipInfos(
                texDesc.format,
                texDesc.dimensions,
                1 /*HgiTextureCpuToGpuOp does one layer at a time*/);

        if (mipInfos.size() > copyOp.mipLevel) {
            HgiMipInfo const& mipInfo = mipInfos[copyOp.mipLevel];

            uint8_t* dst = static_cast<uint8_t*>(
                dstTexture->GetCPUStagingAddress());
            TF_VERIFY(dst && dstTexture->GetStagingBuffer());

            dst += mipInfo.byteOffset;
            const size_t size =
                std::min(copyOp.bufferByteSize, 1 * mipInfo.byteSizePerLayer);
            memcpy(dst, copyOp.cpuSourceBuffer, size);
        }
    }

    // Schedule transfer from staging buffer to device-local texture
    HgiVulkanBuffer* stagingBuffer = dstTexture->GetStagingBuffer();
    if (TF_VERIFY(stagingBuffer, "Invalid staging buffer for texture")) {
        dstTexture->CopyBufferToTexture(
            _commandBuffer, 
            dstTexture->GetStagingBuffer(),
            copyOp.destinationTexelOffset,
            copyOp.mipLevel);
    }
}

void HgiVulkanBlitCmds::CopyBufferGpuToGpu(
    HgiBufferGpuToGpuOp const& copyOp)
{
    _CreateCommandBuffer();

    HgiBufferHandle const& srcBufHandle = copyOp.gpuSourceBuffer;
    HgiVulkanBuffer* srcBuffer =
        static_cast<HgiVulkanBuffer*>(srcBufHandle.Get());

    if (!TF_VERIFY(srcBuffer && srcBuffer->GetVulkanBuffer(),
        "Invalid source buffer handle")) {
        return;
    }

    HgiBufferHandle const& dstBufHandle = copyOp.gpuDestinationBuffer;
    HgiVulkanBuffer* dstBuffer =
        static_cast<HgiVulkanBuffer*>(dstBufHandle.Get());

    if (!TF_VERIFY(dstBuffer && dstBuffer->GetVulkanBuffer(),
        "Invalid destination buffer handle")) {
        return;
    }

    if (copyOp.byteSize == 0) {
        TF_WARN("The size of the data to copy was zero (aborted)");
        return;
    }

    // Copy data from staging buffer to destination (gpu) buffer
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = copyOp.sourceByteOffset;
    copyRegion.dstOffset = copyOp.destinationByteOffset;
    copyRegion.size = copyOp.byteSize;

    vkCmdCopyBuffer(
        _commandBuffer->GetVulkanCommandBuffer(),
        srcBuffer->GetVulkanBuffer(),
        dstBuffer->GetVulkanBuffer(),
        1, // regionCount
        &copyRegion);
}

void HgiVulkanBlitCmds::BlitTexture(HgiTextureHandle src, HgiTextureHandle dst)
{
    _CreateCommandBuffer();

    HgiVulkanTexture* srcTexture =
        static_cast<HgiVulkanTexture*>(src.Get());

    if (!TF_VERIFY(srcTexture && srcTexture->GetImage(),
        "Invalid texture handle")) {
        return;
    }

    HgiVulkanTexture* dstTexture =
        static_cast<HgiVulkanTexture*>(dst.Get());

    if (!TF_VERIFY(srcTexture && srcTexture->GetImage(),
        "Invalid texture handle")) {
        return;
    }

    HgiTextureDesc const& texDesc = srcTexture->GetDescriptor();

    VkOffset3D origin;
    origin.x = 0;
    origin.y = 0;
    origin.z = 0;

    VkOffset3D size;
    size.x = texDesc.dimensions[0];
    size.y = texDesc.dimensions[1];
    size.z = texDesc.dimensions[2];

    VkImageSubresourceLayers imageSub;
    imageSub.baseArrayLayer = 0;
    imageSub.layerCount = 1;
    imageSub.mipLevel = 0;
    imageSub.aspectMask = _GetImageAspectMaskForCopy(texDesc.usage);

    VkImageBlit region;
    region.srcOffsets[0] = origin;
    region.srcOffsets[1] = size;
    region.srcSubresource = imageSub;
    region.dstOffsets[0] = origin;
    region.dstOffsets[1] = size;
    region.dstSubresource = imageSub;

    // Transition src image to TRANSFER_READ
    VkImageLayout oldLayoutSrc = srcTexture->GetImageLayout();
    srcTexture->TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        oldLayoutSrc,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES,  // no pending writes
        VK_ACCESS_TRANSFER_READ_BIT,          // type of access
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    // producer stage
        VK_PIPELINE_STAGE_TRANSFER_BIT);      // consumer stage

    // Transition dst image to TRANSFER_WRITE
    VkImageLayout oldLayoutDst = dstTexture->GetImageLayout();
    dstTexture->TransitionImageBarrier(
        _commandBuffer,
        dstTexture,
        oldLayoutDst,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES,  // no pending writes
        VK_ACCESS_TRANSFER_WRITE_BIT,          // type of access
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    // producer stage
        VK_PIPELINE_STAGE_TRANSFER_BIT);      // consumer stage

    vkCmdBlitImage(_commandBuffer->GetVulkanCommandBuffer(),
        srcTexture->GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstTexture->GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region,
        VK_FILTER_NEAREST);

    // Transition src image back to what it was.
    VkAccessFlags accessSrc = HgiVulkanTexture::GetDefaultAccessFlags(
        srcTexture->GetDescriptor().usage);

    srcTexture->TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        oldLayoutSrc,                        // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES, // no pending writes
        accessSrc,                           // type of access
        VK_PIPELINE_STAGE_TRANSFER_BIT,      // producer stage
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT); // consumer stage

    // Transition dst image back to what it was.
    VkAccessFlags accessDst = HgiVulkanTexture::GetDefaultAccessFlags(
        dstTexture->GetDescriptor().usage);

    dstTexture->TransitionImageBarrier(
        _commandBuffer,
        dstTexture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        oldLayoutDst,                        // transition tex to this layout
        HgiVulkanTexture::NO_PENDING_WRITES, // no pending writes
        accessDst,                           // type of access
        VK_PIPELINE_STAGE_TRANSFER_BIT,      // producer stage
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT); // consumer stage
}

void HgiVulkanBlitCmds::CopyBufferCpuToGpu(
    HgiBufferCpuToGpuOp const& copyOp)
{
    _CreateCommandBuffer();

    if (copyOp.byteSize == 0 ||
        !copyOp.cpuSourceBuffer ||
        !copyOp.gpuDestinationBuffer)
    {
        return;
    }

    HgiVulkanBuffer* buffer = static_cast<HgiVulkanBuffer*>(
        copyOp.gpuDestinationBuffer.Get());

    // If we used GetCPUStagingAddress as the cpuSourceBuffer when the copyOp
    // was created, we can skip the memcpy since the src and dst buffer are
    // the same and dst staging buffer already contains the desired data.
    // See also: HgiBuffer::GetCPUStagingAddress.
    if (!buffer->IsCPUStagingAddress(copyOp.cpuSourceBuffer) ||
        copyOp.sourceByteOffset != copyOp.destinationByteOffset) {

        // Offset into the src buffer.
        const uint8_t* const src =
            static_cast<const uint8_t*>(copyOp.cpuSourceBuffer) +
                copyOp.sourceByteOffset;

        // Offset into the dst buffer.
        uint8_t* const dst =
            static_cast<uint8_t*>(buffer->GetCPUStagingAddress()) +
                copyOp.destinationByteOffset;

        memcpy(dst, src, copyOp.byteSize);
    }

    // Schedule copy data from staging buffer to device-local buffer.
    HgiVulkanBuffer* stagingBuffer = buffer->GetStagingBuffer();

    if (TF_VERIFY(stagingBuffer)) {
        VkBufferCopy copyRegion = {};
        // Note we use the destinationByteOffset as the srcOffset here. The staging buffer
        // should be prepared with the same data layout of the destination buffer.
        copyRegion.srcOffset = copyOp.destinationByteOffset;
        copyRegion.dstOffset = copyOp.destinationByteOffset;
        copyRegion.size = copyOp.byteSize;

        vkCmdCopyBuffer(
            _commandBuffer->GetVulkanCommandBuffer(), 
            stagingBuffer->GetVulkanBuffer(),
            buffer->GetVulkanBuffer(),
            1, 
            &copyRegion);
    }
}

void
HgiVulkanBlitCmds::CopyBufferGpuToCpu(HgiBufferGpuToCpuOp const& copyOp)
{
    _CreateCommandBuffer();

    if (copyOp.byteSize == 0 ||
        !copyOp.cpuDestinationBuffer ||
        !copyOp.gpuSourceBuffer)
    {
        return;
    }

    HgiVulkanBuffer* buffer = static_cast<HgiVulkanBuffer*>(
        copyOp.gpuSourceBuffer.Get());

    // Make sure there is a staging buffer in the buffer by asking for cpuAddr.
    void* cpuAddress = buffer->GetCPUStagingAddress();
    HgiVulkanBuffer* stagingBuffer = buffer->GetStagingBuffer();
    if (!TF_VERIFY(stagingBuffer)) {
        return;
    }

    // Copy from device-local GPU buffer into GPU staging buffer
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = copyOp.sourceByteOffset;
    // No need to use dst offset during intermediate step of copying into 
    // staging buffer.
    copyRegion.dstOffset = 0;
    copyRegion.size = copyOp.byteSize;
    vkCmdCopyBuffer(
        _commandBuffer->GetVulkanCommandBuffer(), 
        buffer->GetVulkanBuffer(),
        stagingBuffer->GetVulkanBuffer(),
        1, 
        &copyRegion);

    // Next schedule a callback when the above GPU-GPU copy completes.

    // Offset into the dst buffer
    char* dst = ((char*) copyOp.cpuDestinationBuffer) +
        copyOp.destinationByteOffset;

    // No need to offset into src buffer since we copied into staging buffer
    // without dst offset.
    const char* src = ((const char*) cpuAddress);

    // bytes to copy
    size_t size = copyOp.byteSize;

    // Copy to cpu buffer when cmd buffer has been executed
    _commandBuffer->AddCompletedHandler(
        [dst, src, size]{ memcpy(dst, src, size);}
    );
}

void
HgiVulkanBlitCmds::CopyTextureToBuffer(HgiTextureToBufferOp const& copyOp)
{
    if (copyOp.byteSize == 0) {
        TF_WARN("The size of the data to copy was zero, aborting copy.");
        return;
    }

    _CreateCommandBuffer();

    HgiVulkanTexture* srcTexture =
        static_cast<HgiVulkanTexture*>(copyOp.gpuSourceTexture.Get());
    if (!TF_VERIFY(srcTexture && srcTexture->GetImage(), "Invalid texture")) {
        return;
    }
    HgiTextureDesc const& texDesc = srcTexture->GetDescriptor();

    HgiVulkanBuffer* dstBuffer =
        static_cast<HgiVulkanBuffer*>(copyOp.gpuDestinationBuffer.Get());
    if (!TF_VERIFY(dstBuffer && dstBuffer->GetVulkanBuffer(),
        "Invalid buffer")) {
        return;
    }

    // Transition image layout to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL.
    VkImageLayout oldLayout = srcTexture->GetImageLayout();
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        /*oldLayout*/oldLayout,
        /*newLayout*/VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        /*producerAccess*/HgiVulkanTexture::GetDefaultAccessFlags(texDesc.usage),
        /*consumerAccess*/VK_ACCESS_TRANSFER_READ_BIT,
        /*producerStage*/VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        /*consumerStage*/VK_PIPELINE_STAGE_TRANSFER_BIT,
        copyOp.mipLevel);

    VkOffset3D origin;
    origin.x = copyOp.sourceTexelOffset[0];
    origin.y = copyOp.sourceTexelOffset[1];
    origin.z = copyOp.sourceTexelOffset[2];

    VkExtent3D size;
    size.width = texDesc.dimensions[0];
    size.height = texDesc.dimensions[1];
    size.depth = texDesc.dimensions[2];

    VkImageSubresourceLayers imageSub;
    imageSub.baseArrayLayer = 0;
    imageSub.layerCount = texDesc.layerCount;
    imageSub.mipLevel = copyOp.mipLevel;
    imageSub.aspectMask = _GetImageAspectMaskForCopy(texDesc.usage);

    VkBufferImageCopy region;
    region.bufferImageHeight = 0; // Buffer is tightly packed, like image
    region.bufferRowLength = 0;   // Buffer is tightly packed, like image
    region.bufferOffset = copyOp.destinationByteOffset;
    region.imageExtent = size;
    region.imageOffset = origin;
    region.imageSubresource = imageSub;

    vkCmdCopyImageToBuffer(
        _commandBuffer->GetVulkanCommandBuffer(),
        srcTexture->GetImage(),
        srcTexture->GetImageLayout(),
        dstBuffer->GetVulkanBuffer(),
        /*regionCount*/1,
        &region
    );

    // Transition image layout back to original layout.
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        srcTexture,
        /*oldLayout*/VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        /*newLayout*/oldLayout,
        /*producerAccess*/VK_ACCESS_TRANSFER_WRITE_BIT,
        /*consumerAccess*/HgiVulkanTexture::GetDefaultAccessFlags(texDesc.usage),
        /*producerStage*/VK_PIPELINE_STAGE_TRANSFER_BIT,
        /*consumerStage*/VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        copyOp.mipLevel);
}

void
HgiVulkanBlitCmds::CopyBufferToTexture(HgiBufferToTextureOp const& copyOp)
{
    if (copyOp.byteSize == 0) {
        TF_WARN("The size of the data to copy was zero, aborting copy.");
        return;
    }

    _CreateCommandBuffer();

    HgiBufferHandle const& srcBufHandle = copyOp.gpuSourceBuffer;
    HgiVulkanBuffer* srcBuffer =
        static_cast<HgiVulkanBuffer*>(srcBufHandle.Get());
    if (!TF_VERIFY(srcBuffer && srcBuffer->GetVulkanBuffer(),
        "Invalid buffer")) {
        return;
    }

    HgiVulkanTexture* dstTexture =
        static_cast<HgiVulkanTexture*>(copyOp.gpuDestinationTexture.Get());
    if (!TF_VERIFY(dstTexture && dstTexture->GetImage(),
        "Invalid texture handle")) {
        return;
    }
    HgiTextureDesc const& texDesc = dstTexture->GetDescriptor();

    // Transition image layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
    VkImageLayout oldLayout = dstTexture->GetImageLayout();
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        dstTexture,
        /*oldLayout*/oldLayout,
        /*newLayout*/VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        /*producerAccess*/HgiVulkanTexture::GetDefaultAccessFlags(texDesc.usage),
        /*consumerAccess*/VK_ACCESS_TRANSFER_WRITE_BIT,
        /*producerStage*/VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        /*consumerStage*/VK_PIPELINE_STAGE_TRANSFER_BIT,
        copyOp.mipLevel);

    VkOffset3D origin;
    origin.x = copyOp.destinationTexelOffset[0];
    origin.y = copyOp.destinationTexelOffset[1];
    origin.z = copyOp.destinationTexelOffset[2];

    VkExtent3D size;
    size.width = texDesc.dimensions[0] - copyOp.destinationTexelOffset[0];
    size.height = texDesc.dimensions[1] - copyOp.destinationTexelOffset[1];
    size.depth = texDesc.dimensions[2] - copyOp.destinationTexelOffset[2];

    VkImageSubresourceLayers imageSub;
    imageSub.baseArrayLayer = 0;
    imageSub.layerCount = texDesc.layerCount;
    imageSub.mipLevel = copyOp.mipLevel;
    imageSub.aspectMask = _GetImageAspectMaskForCopy(texDesc.usage);

    VkBufferImageCopy region;
    region.bufferImageHeight = 0; // Buffer is tightly packed, like image
    region.bufferRowLength = 0;   // Buffer is tightly packed, like image
    region.bufferOffset = copyOp.sourceByteOffset;
    region.imageExtent = size;
    region.imageOffset = origin;
    region.imageSubresource = imageSub;

    vkCmdCopyBufferToImage(
        _commandBuffer->GetVulkanCommandBuffer(),
        srcBuffer->GetVulkanBuffer(),
        dstTexture->GetImage(),
        dstTexture->GetImageLayout(),
        /*regionCount*/1,
        &region);

    // Transition image layout back to original layout.
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        dstTexture,
        /*oldLayout*/VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        /*newLayout*/oldLayout,
        /*producerAccess*/VK_ACCESS_TRANSFER_WRITE_BIT,
        /*consumerAccess*/HgiVulkanTexture::GetDefaultAccessFlags(texDesc.usage),
        /*producerStage*/VK_PIPELINE_STAGE_TRANSFER_BIT,
        /*consumerStage*/VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        copyOp.mipLevel);
}

void
HgiVulkanBlitCmds::GenerateMipMaps(HgiTextureHandle const& texture)
{
    _CreateCommandBuffer();

    HgiVulkanTexture* vkTex = static_cast<HgiVulkanTexture*>(texture.Get());
    HgiVulkanDevice* device = vkTex->GetDevice();

    HgiTextureDesc const& desc = texture->GetDescriptor();

    bool const isDepthBuffer = desc.usage & HgiTextureUsageBitsDepthTarget;
    VkFormat format = HgiVulkanConversions::GetFormat(
        desc.format, isDepthBuffer);
    
    int32_t width = desc.dimensions[0];
    int32_t height = desc.dimensions[1];

    if (desc.layerCount > 1) {
        TF_CODING_ERROR("Missing implementation generating mips for layers");
    }

    // Ensure texture format supports blit src&dst required for mips
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(
        device->GetVulkanPhysicalDevice(), format, &formatProps);
    if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
    {
        TF_CODING_ERROR("Texture format does not support "
                        "blit source and destination");
        return;                    
    }

    VkImageLayout const oldLayout = vkTex->GetImageLayout();

    // Transition first mip to TRANSFER_SRC so we can read it
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        vkTex,
        oldLayout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        HgiVulkanTexture::NO_PENDING_WRITES,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0);

    // Copy down the whole mip chain doing a blit from mip-1 to mip
    for (uint32_t i = 1; i < desc.mipLevels; i++) {
        VkImageBlit imageBlit{};

        // Source
        imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.srcSubresource.layerCount = 1;
        imageBlit.srcSubresource.mipLevel = i - 1;
        imageBlit.srcOffsets[1].x = std::max(width >> (i - 1), 1);
        imageBlit.srcOffsets[1].y = std::max(height >> (i - 1), 1);
        imageBlit.srcOffsets[1].z = 1;

        // Destination
        imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.dstSubresource.layerCount = 1;
        imageBlit.dstSubresource.mipLevel = i;
        imageBlit.dstOffsets[1].x = std::max(width >> i, 1);
        imageBlit.dstOffsets[1].y = std::max(height >> i, 1);
        imageBlit.dstOffsets[1].z = 1;

        // Transition current mip level to image blit destination
        HgiVulkanTexture::TransitionImageBarrier(
            _commandBuffer,
            vkTex,
            oldLayout,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            HgiVulkanTexture::NO_PENDING_WRITES,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            i);

        // Blit from previous level
        vkCmdBlitImage(
            _commandBuffer->GetVulkanCommandBuffer(),
            vkTex->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vkTex->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &imageBlit,
            VK_FILTER_LINEAR);

        // Prepare current mip level as image blit source for next level
        HgiVulkanTexture::TransitionImageBarrier(
            _commandBuffer,
            vkTex,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            i);
    }

    // Return all mips from TRANSFER_SRC to their default (usually SHADER_READ)
    HgiVulkanTexture::TransitionImageBarrier(
        _commandBuffer,
        vkTex,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        HgiVulkanTexture::GetDefaultImageLayout(desc.usage),
        VK_ACCESS_TRANSFER_READ_BIT,
        HgiVulkanTexture::GetDefaultAccessFlags(desc.usage),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
}

void
HgiVulkanBlitCmds::FillBuffer(HgiBufferHandle const& buffer, uint8_t value)
{
    _CreateCommandBuffer();

    HgiVulkanBuffer* buf = static_cast<HgiVulkanBuffer*>(buffer.Get());

    // Convert 8-bit value to 32-bit value e.g. if given 0xff, we want to pass
    // 0xffffffff to vkCmdFillBuffer.
    const uint32_t value32Bit = static_cast<uint32_t>(value) | 
                                static_cast<uint32_t>(value) << 8 | 
                                static_cast<uint32_t>(value) << 16 |
                                static_cast<uint32_t>(value) << 24;

    vkCmdFillBuffer(
        _commandBuffer->GetVulkanCommandBuffer(),
        buf->GetVulkanBuffer(),
        0,
        VK_WHOLE_SIZE,
        value32Bit);
}

void
HgiVulkanBlitCmds::InsertMemoryBarrier(HgiMemoryBarrier barrier)
{
    _CreateCommandBuffer();
    _commandBuffer->InsertMemoryBarrier(barrier);
}

HgiVulkanCommandBuffer*
HgiVulkanBlitCmds::GetCommandBuffer()
{
    return _commandBuffer;
}

bool
HgiVulkanBlitCmds::_Submit(Hgi* hgi, HgiSubmitWaitType wait)
{
    if (!_commandBuffer) {
        return false;
    }

    HgiVulkanDevice* device = _commandBuffer->GetDevice();
    HgiVulkanCommandQueue* queue = device->GetCommandQueue();

    // Submit the GPU work and optionally do CPU - GPU synchronization.
    queue->SubmitToQueue(_commandBuffer, wait);

    return true;
}

void
HgiVulkanBlitCmds::_CreateCommandBuffer()
{
    if (!_commandBuffer) {
        HgiVulkanDevice* device = _hgi->GetPrimaryDevice();
        HgiVulkanCommandQueue* queue = device->GetCommandQueue();
        _commandBuffer = queue->AcquireCommandBuffer();
        TF_VERIFY(_commandBuffer);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
