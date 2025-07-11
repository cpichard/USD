//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hgiVulkan/shaderGenerator.h"
#include "pxr/imaging/hgiVulkan/conversions.h"
#include "pxr/imaging/hgiVulkan/descriptorSetLayouts.h"
#include "pxr/imaging/hgiVulkan/hgi.h"
#include "pxr/imaging/hgi/tokens.h"

#include "pxr/base/trace/trace.h"

PXR_NAMESPACE_OPEN_SCOPE

static const char *
_GetPackedTypeDefinitions()
{
    return
        "\n"
        "struct hgi_ivec3 { int    x, y, z; };\n"
        "struct hgi_vec3  { float  x, y, z; };\n"
        "struct hgi_dvec3 { double x, y, z; };\n"
        "struct hgi_mat3  { float  m00, m01, m02,\n"
        "                          m10, m11, m12,\n"
        "                          m20, m21, m22; };\n"
        "struct hgi_dmat3 { double m00, m01, m02,\n"
        "                          m10, m11, m12,\n"
        "                          m20, m21, m22; };\n";
}

template<typename SectionType, typename ...T>
SectionType *
HgiVulkanShaderGenerator::CreateShaderSection(T && ...t)
{
    std::unique_ptr<SectionType> p =
        std::make_unique<SectionType>(std::forward<T>(t)...);
    SectionType * const result = p.get();
    GetShaderSections()->push_back(std::move(p));
    return result;
}

HgiVulkanShaderGenerator::HgiVulkanShaderGenerator(
    Hgi const *hgi,
    const HgiShaderFunctionDesc &descriptor)
  : HgiShaderGenerator(descriptor)
  , _hgi(hgi)
  , _textureBindIndexStart(0)
  , _inLocationIndex(0)
  , _outLocationIndex(0)
  , _descriptorSetLayoutsAdded(false)
{
    // Write out all GL shaders and add to shader sections

    if (descriptor.shaderStage == HgiShaderStageCompute) {
        int workSizeX = descriptor.computeDescriptor.localSize[0];
        int workSizeY = descriptor.computeDescriptor.localSize[1];
        int workSizeZ = descriptor.computeDescriptor.localSize[2];

        if (workSizeX == 0 || workSizeY == 0 || workSizeZ == 0) {
            workSizeX = 1;
            workSizeY = 1;
            workSizeZ = 1;
        }
      
        _shaderLayoutAttributes.push_back(
            std::string("layout(") +
            "local_size_x = " + std::to_string(workSizeX) + ", "
            "local_size_y = " + std::to_string(workSizeY) + ", "
            "local_size_z = " + std::to_string(workSizeZ) + ") in;\n"
        );
    } else if (descriptor.shaderStage == HgiShaderStageTessellationControl) {
        _shaderLayoutAttributes.emplace_back(
            "layout (vertices = " +
                descriptor.tessellationDescriptor.numVertsPerPatchOut +
                ") out;\n");
    } else if (descriptor.shaderStage == HgiShaderStageTessellationEval) {
        if (descriptor.tessellationDescriptor.patchType ==
                HgiShaderFunctionTessellationDesc::PatchType::Triangles) {
            _shaderLayoutAttributes.emplace_back(
                "layout (triangles) in;\n");
        } else if (descriptor.tessellationDescriptor.patchType ==
                HgiShaderFunctionTessellationDesc::PatchType::Quads) {
            _shaderLayoutAttributes.emplace_back(
                "layout (quads) in;\n");
        } else if (descriptor.tessellationDescriptor.patchType ==
                HgiShaderFunctionTessellationDesc::PatchType::Isolines) {
            _shaderLayoutAttributes.emplace_back(
                "layout (isolines) in;\n");
        }
        if (descriptor.tessellationDescriptor.spacing ==
                HgiShaderFunctionTessellationDesc::Spacing::Equal) {
            _shaderLayoutAttributes.emplace_back(
                "layout (equal_spacing) in;\n");
        } else if (descriptor.tessellationDescriptor.spacing ==
                HgiShaderFunctionTessellationDesc::Spacing::FractionalEven) {
            _shaderLayoutAttributes.emplace_back(
                "layout (fractional_even_spacing) in;\n");
        } else if (descriptor.tessellationDescriptor.spacing ==
                HgiShaderFunctionTessellationDesc::Spacing::FractionalOdd) {
            _shaderLayoutAttributes.emplace_back(
                "layout (fractional_odd_spacing) in;\n");
        }
        // We flip the winding order in HgiVulkan. See
        // HgiVulkanGraphicsCmds::SetViewport for details.
        if (descriptor.tessellationDescriptor.ordering ==
                HgiShaderFunctionTessellationDesc::Ordering::CW) {
            _shaderLayoutAttributes.emplace_back(
                "layout (ccw) in;\n");
        } else if (descriptor.tessellationDescriptor.ordering ==
                HgiShaderFunctionTessellationDesc::Ordering::CCW) {
            _shaderLayoutAttributes.emplace_back(
                "layout (cw) in;\n");
        }
    } else if (descriptor.shaderStage == HgiShaderStageGeometry) {
        if (descriptor.geometryDescriptor.inPrimitiveType ==
            HgiShaderFunctionGeometryDesc::InPrimitiveType::Points) {
            _shaderLayoutAttributes.emplace_back(
                "layout (points) in;\n");
        } else if (descriptor.geometryDescriptor.inPrimitiveType ==
            HgiShaderFunctionGeometryDesc::InPrimitiveType::Lines) {
            _shaderLayoutAttributes.emplace_back(
                "layout (lines) in;\n");
        } else if (descriptor.geometryDescriptor.inPrimitiveType ==
            HgiShaderFunctionGeometryDesc::InPrimitiveType::LinesAdjacency) {
            _shaderLayoutAttributes.emplace_back(
                "layout (lines_adjacency) in;\n");
        } else if (descriptor.geometryDescriptor.inPrimitiveType ==
            HgiShaderFunctionGeometryDesc::InPrimitiveType::Triangles) {
            _shaderLayoutAttributes.emplace_back(
                "layout (triangles) in;\n");
        } else if (descriptor.geometryDescriptor.inPrimitiveType ==
            HgiShaderFunctionGeometryDesc::InPrimitiveType::TrianglesAdjacency){
            _shaderLayoutAttributes.emplace_back(
                "layout (triangles_adjacency) in;\n");
        }

        if (descriptor.geometryDescriptor.outPrimitiveType ==
            HgiShaderFunctionGeometryDesc::OutPrimitiveType::Points) {
            _shaderLayoutAttributes.emplace_back(
                "layout (points, max_vertices = " +
                descriptor.geometryDescriptor.outMaxVertices + ") out;\n");
        } else if (descriptor.geometryDescriptor.outPrimitiveType ==
            HgiShaderFunctionGeometryDesc::OutPrimitiveType::LineStrip) {
            _shaderLayoutAttributes.emplace_back(
                "layout (line_strip, max_vertices = " +
                descriptor.geometryDescriptor.outMaxVertices + ") out;\n");
        } else if (descriptor.geometryDescriptor.outPrimitiveType ==
            HgiShaderFunctionGeometryDesc::OutPrimitiveType::TriangleStrip) {
            _shaderLayoutAttributes.emplace_back(
                "layout (triangle_strip, max_vertices = " +
                descriptor.geometryDescriptor.outMaxVertices + ") out;\n");
        }
    } else if (descriptor.shaderStage == HgiShaderStageFragment) {
        if (descriptor.fragmentDescriptor.earlyFragmentTests) {
            _shaderLayoutAttributes.emplace_back(
                "layout (early_fragment_tests) in;\n");
        }
    }

    // The ordering here is important (buffers before textures), because we
    // need to increment the bind location for resources in the same order
    // as HgiVulkanResourceBindings.
    // In Vulkan buffers and textures cannot have the same binding index.
    _WriteConstantParams(descriptor.constantParams);
    _WriteBuffers(descriptor.buffers);
    _WriteTextures(descriptor.textures);
    _WriteInOuts(descriptor.stageInputs, "in");
    _WriteInOutBlocks(descriptor.stageInputBlocks, "in");
    _WriteInOuts(descriptor.stageOutputs, "out");
    _WriteInOutBlocks(descriptor.stageOutputBlocks, "out");
}

void
HgiVulkanShaderGenerator::_WriteVersion(std::ostream &ss)
{
    const int glslVersion = _hgi->GetCapabilities()->GetShaderVersion();

    ss << "#version " << std::to_string(glslVersion) << "\n";
}

void
HgiVulkanShaderGenerator::_WriteExtensions(std::ostream &ss)
{
    const int glslVersion = _hgi->GetCapabilities()->GetShaderVersion();
    const bool shaderDrawParametersEnabled = _hgi->GetCapabilities()->
        IsSet(HgiDeviceCapabilitiesBitsShaderDrawParameters);
    const bool builtinBarycentricsEnabled = _hgi->GetCapabilities()->
        IsSet(HgiDeviceCapabilitiesBitsBuiltinBarycentrics);

    if (_GetShaderStage() & HgiShaderStageVertex) {
        if (glslVersion < 460 && shaderDrawParametersEnabled) {
            ss << "#extension GL_ARB_shader_draw_parameters : require\n";
        }
        if (shaderDrawParametersEnabled) {
            ss << "int HgiGetBaseVertex() {\n";
            if (glslVersion < 460) { // use ARB extension
                ss << "  return gl_BaseVertexARB;\n";
            } else {
                ss << "  return gl_BaseVertex;\n";
            }
            ss << "}\n";

            ss << "int HgiGetBaseInstance() {\n";
            if (glslVersion < 460) { // use ARB extension
                ss << "  return gl_BaseInstanceARB;\n";
            } else {
                ss << "  return gl_BaseInstance;\n";
            }
            ss << "}\n";
        }
    }

    if (_GetShaderStage() & HgiShaderStageFragment) {
        if (builtinBarycentricsEnabled) {
            ss << "#extension GL_EXT_fragment_shader_barycentric: require\n";
        }
    }
}

void
HgiVulkanShaderGenerator::_WriteMacros(std::ostream &ss)
{
    ss << "#define REF(space,type) inout type\n"
          "#define FORWARD_DECL(func_decl) func_decl\n"
          "#define ATOMIC_LOAD(a) (a)\n"
          "#define ATOMIC_STORE(a, v) (a) = (v)\n"
          "#define ATOMIC_ADD(a, v) atomicAdd(a, v)\n"
          "#define ATOMIC_EXCHANGE(a, v) atomicExchange(a, v)\n"
          "#define ATOMIC_COMP_SWAP(a, expected, desired) atomicCompSwap(a, "
          "expected, desired)\n"
          "#define atomic_int int\n"
          "#define atomic_uint uint\n"
          "#define hd_SampleMask gl_SampleMask[0]\n";

    // Advertise to shader code that we support double precision math
    ss << "\n"
        << "#define HGI_HAS_DOUBLE_TYPE 1\n"
        << "\n";
}

void
HgiVulkanShaderGenerator::_WriteConstantParams(
    const HgiShaderFunctionParamDescVector &parameters)
{
    if (parameters.empty()) {
        return;
    }
    CreateShaderSection<HgiVulkanBlockShaderSection>(
        "ParamBuffer",
        parameters);
}

void
HgiVulkanShaderGenerator::_WriteTextures(
    const HgiShaderFunctionTextureDescVector& textures)
{
    for (const HgiShaderFunctionTextureDesc& desc : textures) {
        HgiShaderSectionAttributeVector attrs = {
            HgiShaderSectionAttribute{
                "binding",
                std::to_string(_textureBindIndexStart + desc.bindIndex)}};

        if (desc.writable) {
            attrs.insert(attrs.begin(), HgiShaderSectionAttribute{
                HgiVulkanConversions::GetImageLayoutFormatQualifier(
                    desc.format), 
                ""});
        }

        const uint32_t textureBindIndex =
            _textureBindIndexStart + desc.bindIndex;

        CreateShaderSection<HgiVulkanTextureShaderSection>(
            desc.nameInShader,
            textureBindIndex,
            desc.dimensions,
            desc.format,
            desc.textureType,
            desc.arraySize,
            desc.writable,
            attrs);

        const VkDescriptorType descriptorType =
            desc.writable
                ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        const uint32_t descriptorCount =
            desc.arraySize > 0
                ? desc.arraySize
                : 1;

        _AddDescriptorSetLayoutBinding(
            textureBindIndex, descriptorType, descriptorCount);
    }
}

void
HgiVulkanShaderGenerator::_WriteBuffers(
    const HgiShaderFunctionBufferDescVector &buffers)
{
    // Extract buffer descriptors and add appropriate buffer sections
    for (size_t i=0; i<buffers.size(); i++) {
        const HgiShaderFunctionBufferDesc &bufferDescription = buffers[i];
        
        const bool isUniformBufferBinding =
            (bufferDescription.binding == HgiBindingTypeUniformValue) ||
            (bufferDescription.binding == HgiBindingTypeUniformArray);

        const std::string arraySize =
            (bufferDescription.arraySize > 0)
                ? std::to_string(bufferDescription.arraySize)
                : std::string();

        const uint32_t bindIndex = bufferDescription.bindIndex;
        
        if (isUniformBufferBinding) {
            const HgiShaderSectionAttributeVector attrs = {
                HgiShaderSectionAttribute{"std140", ""},
                HgiShaderSectionAttribute{"binding", 
                    std::to_string(bindIndex)}};

            CreateShaderSection<HgiVulkanBufferShaderSection>(
                bufferDescription.nameInShader,
                bindIndex,
                bufferDescription.type,
                bufferDescription.binding,
                arraySize,
                false,
                attrs);
        } else {
            const HgiShaderSectionAttributeVector attrs = {
                HgiShaderSectionAttribute{"std430", ""},
                HgiShaderSectionAttribute{"binding", 
                    std::to_string(bindIndex)}};

            CreateShaderSection<HgiVulkanBufferShaderSection>(
                bufferDescription.nameInShader,
                bindIndex,
                bufferDescription.type,
                bufferDescription.binding,
                arraySize,
                bufferDescription.writable,
                attrs);
        }

        // In Vulkan, buffers and textures cannot have the same binding index.
        // Start textures right after the last buffer. 
        // See HgiVulkanResourceBindings for details.
        _textureBindIndexStart =
            std::max(_textureBindIndexStart, bindIndex + 1);

        const VkDescriptorType descriptorType =
            isUniformBufferBinding
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        const uint32_t descriptorCount = 1;

        _AddDescriptorSetLayoutBinding(
            bindIndex, descriptorType, descriptorCount);
    }
}

void
HgiVulkanShaderGenerator::_WriteInOuts(
    const HgiShaderFunctionParamDescVector &parameters,
    const std::string &qualifier) 
{
    // To unify glslfx across different apis, other apis may want these to be 
    // defined, but since they are taken in opengl we ignore them.
    const static std::set<std::string> takenOutParams {
        "gl_Position",
        "gl_FragColor",
        "gl_FragDepth",
        "gl_PointSize",
        "gl_CullDistance",
        "hd_SampleMask",
    };

    // Some params are built-in, but we may want to declare them in the shader 
    // anyway, such as to declare their array size.
    const static std::set<std::string> takenOutParamsToDeclare {
        "gl_ClipDistance"
    };

    const static std::unordered_map<std::string, std::string> takenInParams {
        { HgiShaderKeywordTokens->hdPosition, "gl_Position"},
        { HgiShaderKeywordTokens->hdPointCoord, "gl_PointCoord"},
        { HgiShaderKeywordTokens->hdClipDistance, "gl_ClipDistance"},
        { HgiShaderKeywordTokens->hdCullDistance, "gl_CullDistance"},
        { HgiShaderKeywordTokens->hdVertexID, "gl_VertexIndex"},
        { HgiShaderKeywordTokens->hdInstanceID, "gl_InstanceIndex"},
        { HgiShaderKeywordTokens->hdPrimitiveID, "gl_PrimitiveID"},
        { HgiShaderKeywordTokens->hdSampleID, "gl_SampleID"},
        { HgiShaderKeywordTokens->hdSamplePosition, "gl_SamplePosition"},
        { HgiShaderKeywordTokens->hdFragCoord, "gl_FragCoord"},
        { HgiShaderKeywordTokens->hdBaseVertex, "gl_BaseVertex"},
        { HgiShaderKeywordTokens->hdBaseInstance, "HgiGetBaseInstance()"},
        { HgiShaderKeywordTokens->hdFrontFacing, "gl_FrontFacing"},
        { HgiShaderKeywordTokens->hdLayer, "gl_Layer"},
        { HgiShaderKeywordTokens->hdViewportIndex, "gl_ViewportIndex"},
        { HgiShaderKeywordTokens->hdGlobalInvocationID, "gl_GlobalInvocationID"},
        { HgiShaderKeywordTokens->hdBaryCoordNoPersp, "gl_BaryCoordNoPerspEXT"},
        { HgiShaderKeywordTokens->hdSampleMaskIn, "gl_SampleMaskIn[0]"}
    };

    const bool in_qualifier = qualifier == "in";
    const bool out_qualifier = qualifier == "out";
    for (const HgiShaderFunctionParamDesc &param : parameters) {
        // Skip writing out taken parameter names
        const std::string &paramName = param.nameInShader;
        if (out_qualifier &&
                takenOutParams.find(paramName) != takenOutParams.end()) {
            continue;
        }
        if (out_qualifier && takenOutParamsToDeclare.find(paramName) !=
                             takenOutParamsToDeclare.end()) {
            CreateShaderSection<HgiVulkanMemberShaderSection>(
                paramName,
                param.type,
                param.interpolation,
                param.sampling,
                param.storage,
                HgiShaderSectionAttributeVector(),
                qualifier,
                std::string(),
                param.arraySize);
            continue;
        }
        if (in_qualifier) {
            const std::string &role = param.role;
            auto const& keyword = takenInParams.find(role);
            if (keyword != takenInParams.end()) {
                if (paramName != keyword->second) {
                    CreateShaderSection<HgiVulkanKeywordShaderSection>(
                        paramName,
                        param.type,
                        keyword->second);
                }
                continue;
            }
        }

        // If a location or interstage slot has been specified then add it to 
        // the attributes.
        HgiShaderSectionAttributeVector attrs;
        if (param.location != -1) {
            // If a location has been specified then add it to the attributes.
            attrs.push_back({"location", std::to_string(param.location)});
        } else if (param.interstageSlot != -1) {
            // For interstage parameters use the interstageSlot for location.
            attrs.push_back({"location", std::to_string(param.interstageSlot)});
        } else {
            // Otherwise use shader generator's counter sytem.
            const int32_t locationIndex =
                in_qualifier ? _inLocationIndex++ : _outLocationIndex++;
            attrs.push_back({"location", std::to_string(locationIndex)});
        }

        CreateShaderSection<HgiVulkanMemberShaderSection>(
            paramName,
            param.type,
            param.interpolation,
            param.sampling,
            param.storage,
            attrs,
            qualifier,
            std::string(),
            param.arraySize);
    }
}

void
HgiVulkanShaderGenerator::_WriteInOutBlocks(
    const HgiShaderFunctionParamBlockDescVector &parameterBlocks,
    const std::string &qualifier)
{
    const bool in_qualifier = qualifier == "in";
    const bool out_qualifier = qualifier == "out";

    for (const HgiShaderFunctionParamBlockDesc &p : parameterBlocks) {
        const uint32_t locationIndex = in_qualifier ? 
            _inLocationIndex : _outLocationIndex;

        HgiVulkanMemberShaderSectionPtrVector members;
        for(const HgiShaderFunctionParamBlockDesc::Member &member : p.members) {

            HgiVulkanMemberShaderSection *memberSection =
                CreateShaderSection<HgiVulkanMemberShaderSection>(
                    member.name,
                    member.type,
                    member.interpolation,
                    member.sampling,
                    HgiStorageDefault,
                    HgiShaderSectionAttributeVector(),
                    qualifier,
                    std::string(),
                    std::string(),
                    p.instanceName);
            members.push_back(memberSection);

            if (in_qualifier) {
                _inLocationIndex++;
            } else if (out_qualifier) {
                _outLocationIndex++;
            }
        }

        // If interstage slot has been specified then add it to the attributes.
        HgiShaderSectionAttributeVector attrs;
        if (p.interstageSlot != -1) {
            // For interstage parameters use the interstageSlot for location.
            attrs.push_back({"location", std::to_string(p.interstageSlot)});
        } else {
            // Otherwise use shader generator's counter sytem.
            attrs.push_back({"location", std::to_string(locationIndex)});
        }

        CreateShaderSection<HgiVulkanInterstageBlockShaderSection>(
            p.blockName,
            p.instanceName,
            attrs,
            qualifier,
            p.arraySize,
            members);
    }
}

void
HgiVulkanShaderGenerator::_Execute(std::ostream &ss)
{
    // Version number must be first line in glsl shader
    _WriteVersion(ss);

    _WriteExtensions(ss);

    _WriteMacros(ss);

    ss << _GetPackedTypeDefinitions();
    
    ss << _GetShaderCodeDeclarations();
    
    for (const std::string &attr : _shaderLayoutAttributes) {
        ss << attr;
    }

    HgiVulkanShaderSectionUniquePtrVector* shaderSections = GetShaderSections();
    //For all shader sections, visit the areas defined for all
    //shader apis. We assume all shader apis have a global space
    //section, capabilities to define macros in global space,
    //and abilities to declare some members or functions there
    
    ss << "\n// //////// Global Includes ////////\n";
    for (const std::unique_ptr<HgiVulkanShaderSection>
            &shaderSection : *shaderSections) {
        shaderSection->VisitGlobalIncludes(ss);
    }

    ss << "\n// //////// Global Macros ////////\n";
    for (const std::unique_ptr<HgiVulkanShaderSection>
            &shaderSection : *shaderSections) {
        shaderSection->VisitGlobalMacros(ss);
    }

    ss << "\n// //////// Global Structs ////////\n";
    for (const std::unique_ptr<HgiVulkanShaderSection>
            &shaderSection : *shaderSections) {
        shaderSection->VisitGlobalStructs(ss);
    }

    ss << "\n// //////// Global Member Declarations ////////\n";
    for (const std::unique_ptr<HgiVulkanShaderSection>
            &shaderSection : *shaderSections) {
        shaderSection->VisitGlobalMemberDeclarations(ss);
    }

    ss << "\n// //////// Global Function Definitions ////////\n";
    for (const std::unique_ptr<HgiVulkanShaderSection>
            &shaderSection : *shaderSections) {
        shaderSection->VisitGlobalFunctionDefinitions(ss);
    }

    ss << "\n";

    // write all the original shader
    ss << _GetShaderCode();
}

HgiVulkanShaderSectionUniquePtrVector*
HgiVulkanShaderGenerator::GetShaderSections()
{
    return &_shaderSections;
}

HGIVULKAN_API
HgiVulkanDescriptorSetInfoVector const &
HgiVulkanShaderGenerator::GetDescriptorSetInfo()
{
    TRACE_FUNCTION();

    if (_descriptorSetLayoutsAdded && !_descriptorSetInfo.empty()) {
        _descriptorSetLayoutsAdded = false;

        // This sorting isn't strictly necessary, but it can improve
        // consistency of downstream code which creates descriptor tables.
        using Binding = VkDescriptorSetLayoutBinding;
        std::sort(_descriptorSetInfo[0].bindings.begin(),
                  _descriptorSetInfo[0].bindings.end(),
                  [](Binding const &a, Binding const &b){
                      return a.binding < b.binding;
                  });
    }

    return _descriptorSetInfo;
}

void
HgiVulkanShaderGenerator::_AddDescriptorSetLayoutBinding(
    uint32_t bindingIndex,
    VkDescriptorType descriptorType,
    uint32_t descriptorCount)
{
    if (_descriptorSetInfo.empty()) {
        // For now, all bindings are part of a single descriptor set.
        _descriptorSetInfo.resize(1);
        _descriptorSetInfo.back().setNumber = 0;
    }

    const VkShaderStageFlags stageFlags =
        HgiVulkanConversions::GetShaderStages(_GetShaderStage());

    HgiVulkanDescriptorSetInfo &setInfo = _descriptorSetInfo.back();

    VkDescriptorSetLayoutBinding &bindInfo = setInfo.bindings.emplace_back();
    bindInfo.binding = bindingIndex;
    bindInfo.descriptorType = descriptorType;
    bindInfo.descriptorCount = descriptorCount;
    bindInfo.stageFlags = stageFlags;
    bindInfo.pImmutableSamplers = nullptr;

    _descriptorSetLayoutsAdded = true;
};

PXR_NAMESPACE_CLOSE_SCOPE
