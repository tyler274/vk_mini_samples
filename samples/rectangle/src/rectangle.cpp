/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2014-2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


/*
 This sample creates a 2D rectangle and display it in the middle of the viewport. 
 The rendering is done in gBuffers and the it is the image that is displayed.
 Clear color can be change.
*/


#include <array>
#include <vulkan/vulkan_core.h>
#include <imgui.h>

#define VMA_IMPLEMENTATION
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/dynamicrendering_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvkhl/alloc_vma.hpp"
#include "nvvkhl/application.hpp"
#include "nvvkhl/element_testing.hpp"
#include "nvvkhl/gbuffer.hpp"

#include "_autogen/raster.frag.h"
#include "_autogen/raster.vert.h"

#include <GLFW/glfw3.h>

class RectangleSample : public nvvkhl::IAppElement
{
public:
  RectangleSample()           = default;
  ~RectangleSample() override = default;

  void onAttach(nvvkhl::Application* app) override
  {
    m_app         = app;
    m_device      = m_app->getDevice();
    m_dutil       = std::make_unique<nvvk::DebugUtil>(m_device);                    // Debug utility
    m_alloc       = std::make_unique<nvvkhl::AllocVma>(m_app->getContext().get());  // Allocator
    m_depthFormat = nvvk::findDepthFormat(m_app->getPhysicalDevice());              // Not all depth are supported

    createPipeline();
    createGeometryBuffers();
  }

  void onDetach() override { detroyResources(); }

  void onUIMenu() override
  {
    static bool close_app{false};

    if(ImGui::BeginMenu("File"))
    {
      if(ImGui::MenuItem("Exit", "Ctrl+Q"))
      {
        close_app = true;
      }
      ImGui::EndMenu();
    }

    if(ImGui::IsKeyPressed(ImGuiKey_Q) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
      close_app = true;
    }

    if(close_app)
    {
      m_app->close();
    }
  }

  void onResize(uint32_t width, uint32_t height) override { createGbuffers({width, height}); }

  void onUIRender() override
  {
    if(!m_gBuffers)
      return;

    {  // Setting panel
      ImGui::Begin("Settings");
      ImGui::ColorPicker4("Clear Color", &m_clearColor.float32[0], ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
      ImGui::End();
    }

    {  // Window Title
      static float dirty_timer = 0.0F;
      dirty_timer += ImGui::GetIO().DeltaTime;
      if(dirty_timer > 1.0F)  // Refresh every seconds
      {
        std::array<char, 256> buf{};

        const         int ret = snprintf(buf.data(), buf.size(), "%s %dx%d | %d FPS / %.3fms", PROJECT_NAME,
                           static_cast<int>(m_viewSize.width), static_cast<int>(m_viewSize.height),
                           static_cast<int>(ImGui::GetIO().Framerate), 1000.F / ImGui::GetIO().Framerate);
        assert(ret > 0);
        glfwSetWindowTitle(m_app->getWindowHandle(), buf.data());
        dirty_timer = 0;
      }
    }

    {  // Display the G-Buffer image
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
      ImGui::Begin("Viewport");
      ImGui::Image(m_gBuffers->getDescriptorSet(), ImGui::GetContentRegionAvail());
      ImGui::End();
      ImGui::PopStyleVar();
    }
  }

  void onRender(VkCommandBuffer cmd) override
  {
    if(!m_gBuffers)
      return;

    const     nvvk::DebugUtil::ScopedCmdLabel sdbg = m_dutil->DBG_SCOPE(cmd);

    nvvk::createRenderingInfo r_info({{0, 0}, m_viewSize}, {m_gBuffers->getColorImageView()}, m_gBuffers->getDepthImageView(),
                                     VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR, m_clearColor);
    r_info.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd, &r_info);
    m_app->setViewport(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    const     VkDeviceSize offsets{0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertices.buffer, &offsets);
    vkCmdBindIndexBuffer(cmd, m_indices.buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);
  }

private:
  struct Vertex
  {
    nvmath::vec2f pos;
    nvmath::vec3f color;
  };

  void createPipeline()
  {
    const     VkPipelineLayoutCreateInfo create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_device, &create_info, nullptr, &m_pipelineLayout);

    VkPipelineRenderingCreateInfo prend_info{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    prend_info.colorAttachmentCount    = 1;
    prend_info.pColorAttachmentFormats = &m_colorFormat;
    prend_info.depthAttachmentFormat   = m_depthFormat;

    nvvk::GraphicsPipelineState pstate;
    pstate.addBindingDescriptions({{0, sizeof(Vertex)}});
    pstate.addAttributeDescriptions({
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, pos))},       // Position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, color))},  // Color
    });

    // Shader sources, pre-compiled to Spir-V (see Makefile)
    nvvk::GraphicsPipelineGenerator pgen(m_device, m_pipelineLayout, prend_info, pstate);
    pgen.addShader(std::vector<uint32_t>{std::begin(raster_vert), std::end(raster_vert)}, VK_SHADER_STAGE_VERTEX_BIT);
    pgen.addShader(std::vector<uint32_t>{std::begin(raster_frag), std::end(raster_frag)}, VK_SHADER_STAGE_FRAGMENT_BIT);

    m_graphicsPipeline = pgen.createPipeline();
    m_dutil->setObjectName(m_graphicsPipeline, "Graphics");
    pgen.clearShaders();
  }

  void createGbuffers(VkExtent2D size)
  {
    m_viewSize = size;

    m_gBuffers = std::make_unique<nvvkhl::GBuffer>(m_device, m_alloc.get(), m_viewSize, m_colorFormat, m_depthFormat);
  }

  void createGeometryBuffers()
  {
    const std::vector<Vertex>   vertices = {{{-0.5F, -0.5F}, {1.0F, 1.0F, 0.0F}},
                                            {{0.5F, -0.5F}, {0.0F, 1.0F, 1.0F}},
                                            {{0.5F, 0.5F}, {1.0F, 0.0F, 1.0F}},
                                            {{-0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}}};
    const std::vector<uint16_t> indices  = {0, 2, 1, 2, 0, 3};

    {
      VkCommandBuffer cmd = m_app->createTempCmdBuffer();
      m_vertices          = m_alloc->createBuffer(cmd, vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      m_indices           = m_alloc->createBuffer(cmd, indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      m_app->submitAndWaitTempCmdBuffer(cmd);
      m_dutil->DBG_NAME(m_vertices.buffer);
      m_dutil->DBG_NAME(m_indices.buffer);
    }
  }

  void detroyResources()
  {
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);

    m_alloc->destroy(m_vertices);
    m_alloc->destroy(m_indices);
    m_vertices = {};
    m_indices  = {};
    m_gBuffers.reset();
  }

  nvvkhl::Application* m_app{nullptr};

  std::unique_ptr<nvvkhl::GBuffer>  m_gBuffers;
  std::unique_ptr<nvvk::DebugUtil>  m_dutil;
  std::shared_ptr<nvvkhl::AllocVma> m_alloc;

  VkExtent2D        m_viewSize{0, 0};
  VkFormat          m_colorFormat      = VK_FORMAT_R8G8B8A8_UNORM;  // Color format of the image
  VkFormat          m_depthFormat      = VK_FORMAT_UNDEFINED;       // Depth format of the depth buffer
  VkPipelineLayout  m_pipelineLayout   = VK_NULL_HANDLE;            // The description of the pipeline
  VkPipeline        m_graphicsPipeline = VK_NULL_HANDLE;            // The graphic pipeline to render
  nvvk::Buffer      m_vertices;                                     // Buffer of the vertices
  nvvk::Buffer      m_indices;                                      // Buffer of the indices
  VkClearColorValue m_clearColor{{0.1F, 0.4F, 0.1F, 1.0F}};         // Clear color
  VkDevice          m_device = VK_NULL_HANDLE;                      // Convenient
};


int main(int argc, char** argv)
{
  nvvkhl::ApplicationCreateInfo spec;
  spec.name             = PROJECT_NAME " Example";
  spec.vSync            = true;
  spec.vkSetup.apiMajor = 1;
  spec.vkSetup.apiMinor = 3;

  // Create the application
  auto app = std::make_unique<nvvkhl::Application>(spec);

  auto test = std::make_shared<nvvkhl::ElementTesting>(argc, argv);
  app->addElement(test);
  app->addElement(std::make_shared<RectangleSample>());

  app->run();
  app.reset();

  return test->errorCode();
}
