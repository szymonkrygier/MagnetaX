// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanPipeline.h"

#if MX_GRAPHICS_VULKAN
#include <vector>

namespace
{
    VkShaderModule CreateShaderModule(VkDevice device, const uint32* data, usize dataSize)
    {
        if (!device || !data || dataSize == 0 || dataSize % sizeof(uint32) != 0) return VK_NULL_HANDLE;

        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = dataSize;
        shaderInfo.pCode = data;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule) != VK_SUCCESS) return VK_NULL_HANDLE;

        return shaderModule;
    }
}

bool VulkanPipeline::Create(VkDevice _device, const VulkanPipelineCreateInfo& createInfo)
{
    if (!_device) return false;
    if (!createInfo.vertexShader || createInfo.vertexShaderSize == 0) return false;
    if (createInfo.fragmentShader && createInfo.fragmentShaderSize == 0) return false;
    if (createInfo.vertexBindingCount && !createInfo.vertexBindings) return false;
    if (createInfo.vertexAttributeCount && !createInfo.vertexAttributes) return false;
    if (createInfo.colorFormatCount && !createInfo.colorFormats) return false;
    if (createInfo.descriptorSetLayoutCount && !createInfo.descriptorSetLayouts) return false;
    if (createInfo.pushConstantRangeCount && !createInfo.pushConstantRanges) return false;
    if ((createInfo.depthTest || createInfo.depthWrite) && createInfo.depthFormat == VK_FORMAT_UNDEFINED) return false;

    Destroy();

    device = _device;

    VkShaderModule vertexShaderModule = CreateShaderModule(device, createInfo.vertexShader, createInfo.vertexShaderSize);

    if (!vertexShaderModule)
    {
        Destroy();
        return false;
    }

    VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;

    if (createInfo.fragmentShader)
    {
        fragmentShaderModule = CreateShaderModule(device, createInfo.fragmentShader, createInfo.fragmentShaderSize);

        if (!fragmentShaderModule)
        {
            vkDestroyShaderModule(device, vertexShaderModule, nullptr);
            Destroy();
            return false;
        }
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    uint32 shaderStageCount = 1;

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShaderModule;
    shaderStages[0].pName = "main";

    if (fragmentShaderModule)
    {
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentShaderModule;
        shaderStages[1].pName = "main";

        shaderStageCount = 2;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = createInfo.vertexBindingCount;
    vertexInputInfo.pVertexBindingDescriptions = createInfo.vertexBindings;
    vertexInputInfo.vertexAttributeDescriptionCount = createInfo.vertexAttributeCount;
    vertexInputInfo.pVertexAttributeDescriptions = createInfo.vertexAttributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportInfo{};
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportInfo.viewportCount = 1;
    viewportInfo.scissorCount = 1;

    VkDynamicState dynamicStates[3] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    uint32 dynamicStateCount = 2;

    if (createInfo.depthBias)
    {
        dynamicStates[dynamicStateCount] = VK_DYNAMIC_STATE_DEPTH_BIAS;
        ++dynamicStateCount;
    }

    VkPipelineDynamicStateCreateInfo dynamicInfo{};
    dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.dynamicStateCount = dynamicStateCount;
    dynamicInfo.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterInfo{};
    rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
    //rasterInfo.cullMode = VK_CULL_MODE_NONE;
    rasterInfo.cullMode = createInfo.cullMode;
    rasterInfo.frontFace = createInfo.frontFace;
    rasterInfo.lineWidth = 1.0f;
    rasterInfo.depthBiasEnable = createInfo.depthBias;

    VkPipelineMultisampleStateCreateInfo multisampleInfo{};
    multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.rasterizationSamples = createInfo.sampleCount;
    multisampleInfo.sampleShadingEnable = createInfo.sampleShading ? VK_TRUE : VK_FALSE;
    multisampleInfo.minSampleShading = createInfo.sampleShading ? 1.0f : 0.0f;

    VkPipelineDepthStencilStateCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthInfo.depthTestEnable = createInfo.depthTest;
    depthInfo.depthWriteEnable = createInfo.depthWrite;
    depthInfo.depthCompareOp = createInfo.depthCompareOp;

    std::vector<VkPipelineColorBlendAttachmentState> colorAttachments(createInfo.colorFormatCount);

    for (VkPipelineColorBlendAttachmentState& colorAttachment : colorAttachments)
    {
        colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        if (createInfo.alphaBlending)
        {
            colorAttachment.blendEnable = VK_TRUE;
            colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }

    VkPipelineColorBlendStateCreateInfo blendInfo{};
    blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.attachmentCount = createInfo.colorFormatCount;
    blendInfo.pAttachments = colorAttachments.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = createInfo.descriptorSetLayoutCount;
    layoutInfo.pSetLayouts = createInfo.descriptorSetLayouts;
    layoutInfo.pushConstantRangeCount = createInfo.pushConstantRangeCount;
    layoutInfo.pPushConstantRanges = createInfo.pushConstantRanges;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, vertexShaderModule, nullptr);
        if (fragmentShaderModule) vkDestroyShaderModule(device, fragmentShaderModule, nullptr);

        Destroy();
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = createInfo.colorFormatCount;
    renderingInfo.pColorAttachmentFormats = createInfo.colorFormats;
    renderingInfo.depthAttachmentFormat = createInfo.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = shaderStageCount;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pRasterizationState = &rasterInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;
    pipelineInfo.pDepthStencilState = &depthInfo;
    pipelineInfo.pColorBlendState = &blendInfo;
    pipelineInfo.pDynamicState = &dynamicInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.pNext = &renderingInfo;

    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, vertexShaderModule, nullptr);
    if (fragmentShaderModule) vkDestroyShaderModule(device, fragmentShaderModule, nullptr);

    if (result != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    return true;
}

bool VulkanPipeline::Create(VkDevice _device, const VulkanComputePipelineCreateInfo& createInfo)
{
    if (!_device || !createInfo.computeShader || createInfo.computeShaderSize == 0) return false;
    if (createInfo.descriptorSetLayoutCount && !createInfo.descriptorSetLayouts) return false;
    if (createInfo.pushConstantRangeCount && !createInfo.pushConstantRanges) return false;

    Destroy();

    device = _device;

    VkShaderModule computeShaderModule = CreateShaderModule(device, createInfo.computeShader, createInfo.computeShaderSize);

    if (!computeShaderModule)
    {
        Destroy();
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = createInfo.descriptorSetLayoutCount;
    layoutInfo.pSetLayouts = createInfo.descriptorSetLayouts;
    layoutInfo.pushConstantRangeCount = createInfo.pushConstantRangeCount;
    layoutInfo.pPushConstantRanges = createInfo.pushConstantRanges;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, computeShaderModule, nullptr);
        Destroy();
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = computeShaderModule;
    shaderStage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;

    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, computeShaderModule, nullptr);

    if (result != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanPipeline::Destroy()
{
    if (device)
    {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }

    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;

    device = VK_NULL_HANDLE;
}
#endif
