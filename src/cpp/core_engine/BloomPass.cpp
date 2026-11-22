#include "../../../include/core_engine/BloomPass.hpp"
#include <algorithm>
#include <cmath>

namespace ilmeee {

namespace {
constexpr VkFormat kLevelFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t kMaxSets = 96; // ~10 per target view
} // namespace

bool BloomPass::Init(VkDevice device, VkFormat targetFormat,
                     const ShaderLoader &load,
                     const MemoryTypeFinder &memoryType, std::string *error) {
  Destroy();
  device_ = device;
  targetFormat_ = targetFormat;
  memoryType_ = memoryType;
  const auto fail = [&](const std::string &why) {
    if (error)
      *error = why;
    Destroy();
    device_ = device; // keep for a later Init
    return false;
  };
  if (device == VK_NULL_HANDLE)
    return fail("no Vulkan device");
  try {
    vert_ = load("assets/shaders/vulkan/bloom.vert.spv");
    down_ = load("assets/shaders/vulkan/bloom_down.frag.spv");
    up_ = load("assets/shaders/vulkan/bloom_up.frag.spv");
  } catch (const std::exception &e) {
    return fail(e.what());
  }

  VkSamplerCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.maxLod = 0.0f;
  if (vkCreateSampler(device_, &si, nullptr, &sampler_) != VK_SUCCESS)
    return fail("sampler");

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo li{};
  li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  li.bindingCount = 1;
  li.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &setLayout_) !=
      VK_SUCCESS)
    return fail("descriptor set layout");

  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            kMaxSets};
  VkDescriptorPoolCreateInfo pi{};
  pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pi.maxSets = kMaxSets;
  pi.poolSizeCount = 1;
  pi.pPoolSizes = &size;
  if (vkCreateDescriptorPool(device_, &pi, nullptr, &pool_) != VK_SUCCESS)
    return fail("descriptor pool");

  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(float) * 12};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &setLayout_;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &push;
  if (vkCreatePipelineLayout(device_, &pli, nullptr, &layout_) != VK_SUCCESS)
    return fail("pipeline layout");

  clearPass_ = MakePass(kLevelFormat, false);
  loadPass_ = MakePass(kLevelFormat, true);
  targetPass_ = MakePass(targetFormat_, true);
  if (!clearPass_ || !loadPass_ || !targetPass_)
    return fail("render pass");
  downPipeline_ = MakePipeline(clearPass_, down_, false);
  upPipeline_ = MakePipeline(loadPass_, up_, true);
  compositePipeline_ = MakePipeline(targetPass_, up_, true);
  if (!downPipeline_ || !upPipeline_ || !compositePipeline_)
    return fail("pipeline");
  ready_ = true;
  return true;
}

void BloomPass::Destroy() {
  if (device_ == VK_NULL_HANDLE)
    return;
  for (auto &[view, chain] : chains_)
    DestroyChain(chain);
  chains_.clear();
  for (VkPipeline *p : {&downPipeline_, &upPipeline_, &compositePipeline_})
    if (*p) {
      vkDestroyPipeline(device_, *p, nullptr);
      *p = VK_NULL_HANDLE;
    }
  for (VkRenderPass *p : {&clearPass_, &loadPass_, &targetPass_})
    if (*p) {
      vkDestroyRenderPass(device_, *p, nullptr);
      *p = VK_NULL_HANDLE;
    }
  for (VkShaderModule *m : {&vert_, &down_, &up_})
    if (*m) {
      vkDestroyShaderModule(device_, *m, nullptr);
      *m = VK_NULL_HANDLE;
    }
  if (layout_)
    vkDestroyPipelineLayout(device_, layout_, nullptr);
  if (pool_)
    vkDestroyDescriptorPool(device_, pool_, nullptr);
  if (setLayout_)
    vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
  if (sampler_)
    vkDestroySampler(device_, sampler_, nullptr);
  layout_ = VK_NULL_HANDLE;
  pool_ = VK_NULL_HANDLE;
  setLayout_ = VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
  ready_ = false;
}

VkRenderPass BloomPass::MakePass(VkFormat format, bool load) {
  VkAttachmentDescription color{};
  color.format = format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD
                      : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  // Everything bloom touches rests in SHADER_READ_ONLY between passes.
  color.initialLayout = load ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  VkSubpassDependency deps[2]{};
  // Previous pass wrote (or sampled) this image / the one we sample.
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo ri{};
  ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  ri.attachmentCount = 1;
  ri.pAttachments = &color;
  ri.subpassCount = 1;
  ri.pSubpasses = &sub;
  ri.dependencyCount = 2;
  ri.pDependencies = deps;
  VkRenderPass pass = VK_NULL_HANDLE;
  if (vkCreateRenderPass(device_, &ri, nullptr, &pass) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return pass;
}

VkPipeline BloomPass::MakePipeline(VkRenderPass pass, VkShaderModule frag,
                                   bool additive) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert_;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  if (additive) {
    // Colour adds up; alpha is left as it was (shader writes 0).
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  ds.dynamicStateCount = 2;
  ds.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo gi{};
  gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gi.stageCount = 2;
  gi.pStages = stages;
  gi.pVertexInputState = &vi;
  gi.pInputAssemblyState = &ia;
  gi.pViewportState = &vp;
  gi.pRasterizationState = &rs;
  gi.pMultisampleState = &ms;
  gi.pColorBlendState = &cb;
  gi.pDynamicState = &ds;
  gi.layout = layout_;
  gi.renderPass = pass;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gi, nullptr,
                                &pipeline) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return pipeline;
}

VkDescriptorSet BloomPass::AllocateSet(VkImageView view) {
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &setLayout_;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  VkDescriptorImageInfo image{sampler_, view,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  return set;
}

bool BloomPass::CreateChain(Chain &chain, VkImageView target, int w, int h) {
  chain.w = w;
  chain.h = h;
  chain.sourceSet = AllocateSet(target);
  VkFramebufferCreateInfo fi{};
  fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fi.renderPass = targetPass_;
  fi.attachmentCount = 1;
  fi.pAttachments = &target;
  fi.width = (uint32_t)w;
  fi.height = (uint32_t)h;
  fi.layers = 1;
  if (!chain.sourceSet ||
      vkCreateFramebuffer(device_, &fi, nullptr, &chain.composite) !=
          VK_SUCCESS)
    return false;
  int lw = w, lh = h;
  for (int i = 0; i < BloomSettings::kMaxIterations; ++i) {
    lw = std::max(1, lw / 2);
    lh = std::max(1, lh / 2);
    if (i > 0 && (lw < 2 || lh < 2))
      break;
    Level level;
    level.w = lw;
    level.h = lh;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kLevelFormat;
    ii.extent = {(uint32_t)lw, (uint32_t)lh, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(device_, &ii, nullptr, &level.image) != VK_SUCCESS) {
      chain.levels.push_back(level);
      return false;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device_, level.image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    try {
      ai.memoryTypeIndex =
          memoryType_(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (...) {
      chain.levels.push_back(level);
      return false;
    }
    if (vkAllocateMemory(device_, &ai, nullptr, &level.memory) != VK_SUCCESS) {
      chain.levels.push_back(level);
      return false;
    }
    vkBindImageMemory(device_, level.image, level.memory, 0);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = level.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kLevelFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const bool viewOk =
        vkCreateImageView(device_, &vi, nullptr, &level.view) == VK_SUCCESS;
    if (viewOk) {
      VkFramebufferCreateInfo lf = fi;
      lf.renderPass = clearPass_; // compatible with loadPass_ too
      lf.pAttachments = &level.view;
      lf.width = (uint32_t)lw;
      lf.height = (uint32_t)lh;
      vkCreateFramebuffer(device_, &lf, nullptr, &level.framebuffer);
      level.set = AllocateSet(level.view);
    }
    chain.levels.push_back(level);
    if (!viewOk || !level.framebuffer || !level.set)
      return false;
  }
  return !chain.levels.empty();
}

void BloomPass::DestroyChain(Chain &chain) {
  for (auto &level : chain.levels) {
    if (level.set)
      vkFreeDescriptorSets(device_, pool_, 1, &level.set);
    if (level.framebuffer)
      vkDestroyFramebuffer(device_, level.framebuffer, nullptr);
    if (level.view)
      vkDestroyImageView(device_, level.view, nullptr);
    if (level.image)
      vkDestroyImage(device_, level.image, nullptr);
    if (level.memory)
      vkFreeMemory(device_, level.memory, nullptr);
  }
  chain.levels.clear();
  if (chain.sourceSet)
    vkFreeDescriptorSets(device_, pool_, 1, &chain.sourceSet);
  if (chain.composite)
    vkDestroyFramebuffer(device_, chain.composite, nullptr);
  chain.sourceSet = VK_NULL_HANDLE;
  chain.composite = VK_NULL_HANDLE;
}

void BloomPass::Release(VkImageView targetView) {
  const auto it = chains_.find(targetView);
  if (it == chains_.end())
    return;
  DestroyChain(it->second);
  chains_.erase(it);
}

void BloomPass::Pass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer fb,
                     int w, int h, VkPipeline pipeline, VkDescriptorSet set,
                     const float (&push)[12]) {
  VkRenderPassBeginInfo rp{};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp.renderPass = pass;
  rp.framebuffer = fb;
  rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport{0, 0, (float)w, (float)h, 0, 1};
  VkRect2D scissor{{0, 0}, {(uint32_t)w, (uint32_t)h}};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                          &set, 0, nullptr);
  vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                     sizeof(push), push);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);
}

void BloomPass::Record(VkCommandBuffer cmd, VkImageView targetView, int w,
                       int h, const BloomSettings &input) {
  if (!ready_ || !input.enabled || targetView == VK_NULL_HANDLE || w < 4 ||
      h < 4)
    return;
  BloomSettings s = input;
  s.Clamp();
  if (s.intensity <= 0.0f)
    return;
  auto it = chains_.find(targetView);
  if (it != chains_.end() && (it->second.w != w || it->second.h != h)) {
    DestroyChain(it->second); // same handle, new size (unusual)
    chains_.erase(it);
    it = chains_.end();
  }
  if (it == chains_.end()) {
    Chain chain;
    if (!CreateChain(chain, targetView, w, h)) {
      DestroyChain(chain);
      ready_ = false; // out of memory/descriptors: stop trying every frame
      return;
    }
    it = chains_.emplace(targetView, std::move(chain)).first;
  }
  Chain &chain = it->second;
  const int levels = std::min<int>(s.iterations, (int)chain.levels.size());

  // 1. Prefilter (threshold) into level 0.
  const float knee = std::max(s.threshold * s.softKnee, 1e-4f);
  {
    const float push[12] = {1.0f / w, 1.0f / h, s.threshold, knee,
                            1, 0, 0, 0, 1, 1, 1, 1};
    const Level &dst = chain.levels[0];
    Pass(cmd, clearPass_, dst.framebuffer, dst.w, dst.h, downPipeline_,
         chain.sourceSet, push);
  }
  // 2. Downsample.
  for (int i = 1; i < levels; ++i) {
    const Level &src = chain.levels[(size_t)i - 1], &dst = chain.levels[(size_t)i];
    const float push[12] = {1.0f / src.w, 1.0f / src.h, 0, 0,
                            0, 0, 0, 0, 1, 1, 1, 1};
    Pass(cmd, clearPass_, dst.framebuffer, dst.w, dst.h, downPipeline_,
         src.set, push);
  }
  // 3. Upsample, each smaller level added onto the next larger one with
  // weight `radius` (higher = wider, softer glow).
  for (int i = levels - 1; i >= 1; --i) {
    const Level &src = chain.levels[(size_t)i], &dst = chain.levels[(size_t)i - 1];
    const float push[12] = {1.0f / src.w, 1.0f / src.h, 0, 0,
                            s.radius, 0, 0, 0, 1, 1, 1, 1};
    Pass(cmd, loadPass_, dst.framebuffer, dst.w, dst.h, upPipeline_, src.set,
         push);
  }
  // 4. Composite. Level 0 now holds sum(radius^k * level k); dividing by
  // the sum of weights keeps `intensity` independent of radius/iterations.
  float weights = 0.0f;
  for (int k = 0; k < levels; ++k)
    weights += std::pow(s.radius, (float)k);
  const Level &src = chain.levels[0];
  const float scale = s.intensity / std::max(weights, 1e-4f);
  const float push[12] = {1.0f / src.w, 1.0f / src.h, 0, 0,
                          scale, 0, 0, 0, s.tint.r, s.tint.g, s.tint.b, 1};
  Pass(cmd, targetPass_, chain.composite, w, h, compositePipeline_, src.set,
       push);
}

} // namespace ilmeee
