/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 * 
 * MIT License
 * Copyright (c) 2022-2023 Elie Michel
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "Application.h"
#include "ResourceManager.h"

#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <iostream>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <string>
#include <array>

using namespace wgpu;
using VertexAttributes = ResourceManager::VertexAttributes;

constexpr float PI = 3.14159265358979323846f;

// The raw GLFW callback
void onWindowResize(GLFWwindow* m_window, int /* width */, int /* height */) {
	auto pApp = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_window));
	if (pApp != nullptr) pApp->onResize();
}

void onWindowMouseMove(GLFWwindow* m_window, double xpos, double ypos) {
	auto pApp = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_window));
	if (pApp != nullptr) pApp->onMouseMove(xpos, ypos);
}
void onWindowMouseButton(GLFWwindow* m_window, int button, int action, int mods) {
	auto pApp = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_window));
	if (pApp != nullptr) pApp->onMouseButton(button, action, mods);
}
void onWindowScroll(GLFWwindow* m_window, double xoffset, double yoffset) {
	auto pApp = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_window));
	if (pApp != nullptr) pApp->onScroll(xoffset, yoffset);
}

bool Application::onInit() {
	m_instance = createInstance(InstanceDescriptor{});
	if (!m_instance) {
		std::cerr << "Could not initialize WebGPU!" << std::endl;
		return false;
	}

	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	m_window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);
	if (!m_window) {
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}

	std::cout << "Requesting adapter..." << std::endl;
	m_surface = glfwGetWGPUSurface(m_instance, m_window);
	RequestAdapterOptions adapterOpts{};
	adapterOpts.compatibleSurface = m_surface;
	m_adapter = m_instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << m_adapter << std::endl;

	SupportedLimits supportedLimits;
	m_adapter.getLimits(&supportedLimits);

	std::cout << "Requesting device..." << std::endl;
	RequiredLimits requiredLimits = Default;
	requiredLimits.limits.maxVertexAttributes = 4;
	requiredLimits.limits.maxVertexBuffers = 1;
	requiredLimits.limits.maxBufferSize = 150000 * sizeof(VertexAttributes);
	requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.maxInterStageShaderComponents = 8;
	requiredLimits.limits.maxBindGroups = 1;
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
	requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
	// Allow textures up to 2K
	requiredLimits.limits.maxTextureDimension1D = 2048;
	requiredLimits.limits.maxTextureDimension2D = 2048;
	requiredLimits.limits.maxTextureArrayLayers = 1;
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 1;
	requiredLimits.limits.maxSamplersPerShaderStage = 1;

	DeviceDescriptor deviceDesc;
	deviceDesc.label = "My Device";
	deviceDesc.requiredFeaturesCount = 0;
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = "The default queue";
	m_device = m_adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << m_device << std::endl;

	// Add an error callback for more debug info
	m_errorCallbackHandle = m_device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
		std::cout << "Device error: type " << type;
		if (message) std::cout << " (message: " << message << ")";
		std::cout << std::endl;
	});

	m_queue = m_device.getQueue();

	buildSwapChain();
	buildDepthBuffer();

	std::cout << "Creating shader module..." << std::endl;
	m_shaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", m_device);
	std::cout << "Shader module: " << m_shaderModule << std::endl;

	std::cout << "Creating render pipeline..." << std::endl;
	RenderPipelineDescriptor pipelineDesc;

	// Vertex fetch
	std::vector<VertexAttribute> vertexAttribs(4);

	// Position attribute
	vertexAttribs[0].shaderLocation = 0;
	vertexAttribs[0].format = VertexFormat::Float32x3;
	vertexAttribs[0].offset = 0;

	// Normal attribute
	vertexAttribs[1].shaderLocation = 1;
	vertexAttribs[1].format = VertexFormat::Float32x3;
	vertexAttribs[1].offset = offsetof(VertexAttributes, normal);

	// Color attribute
	vertexAttribs[2].shaderLocation = 2;
	vertexAttribs[2].format = VertexFormat::Float32x3;
	vertexAttribs[2].offset = offsetof(VertexAttributes, color);

	// UV attribute
	vertexAttribs[3].shaderLocation = 3;
	vertexAttribs[3].format = VertexFormat::Float32x2;
	vertexAttribs[3].offset = offsetof(VertexAttributes, uv);

	VertexBufferLayout vertexBufferLayout;
	vertexBufferLayout.attributeCount = (uint32_t)vertexAttribs.size();
	vertexBufferLayout.attributes = vertexAttribs.data();
	vertexBufferLayout.arrayStride = sizeof(VertexAttributes);
	vertexBufferLayout.stepMode = VertexStepMode::Vertex;

	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;

	pipelineDesc.vertex.module = m_shaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = FrontFace::CCW;
	pipelineDesc.primitive.cullMode = CullMode::None;

	FragmentState fragmentState;
	pipelineDesc.fragment = &fragmentState;
	fragmentState.module = m_shaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;

	BlendState blendState;
	blendState.color.srcFactor = BlendFactor::SrcAlpha;
	blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = BlendOperation::Add;
	blendState.alpha.srcFactor = BlendFactor::Zero;
	blendState.alpha.dstFactor = BlendFactor::One;
	blendState.alpha.operation = BlendOperation::Add;

	ColorTargetState colorTarget;
	colorTarget.format = m_swapChainFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = ColorWriteMask::All;

	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	DepthStencilState depthStencilState = Default;
	depthStencilState.depthCompare = CompareFunction::Less;
	depthStencilState.depthWriteEnabled = true;
	depthStencilState.format = m_depthTextureFormat;
	depthStencilState.stencilReadMask = 0;
	depthStencilState.stencilWriteMask = 0;
	
	pipelineDesc.depthStencil = &depthStencilState;

	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	// Create binding layouts

	std::vector<BindGroupLayoutEntry> bindingLayoutEntries(3, Default);

	// The uniform buffer binding that we already had
	BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
	bindingLayout.binding = 0;
	bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
	bindingLayout.buffer.type = BufferBindingType::Uniform;
	bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

	// The texture binding
	BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
	textureBindingLayout.binding = 1;
	textureBindingLayout.visibility = ShaderStage::Fragment;
	textureBindingLayout.texture.sampleType = TextureSampleType::Float;
	textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	// The texture sampler binding
	BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
	samplerBindingLayout.binding = 2;
	samplerBindingLayout.visibility = ShaderStage::Fragment;
	samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

	// Create a bind group layout
	BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
	bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
	BindGroupLayout bindGroupLayout = m_device.createBindGroupLayout(bindGroupLayoutDesc);

	// Create the pipeline layout
	PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&bindGroupLayout;
	PipelineLayout layout = m_device.createPipelineLayout(layoutDesc);
	pipelineDesc.layout = layout;

	m_pipeline = m_device.createRenderPipeline(pipelineDesc);
	std::cout << "Render pipeline: " << m_pipeline << std::endl;

	// Create a sampler
	SamplerDescriptor samplerDesc;
	samplerDesc.addressModeU = AddressMode::Repeat;
	samplerDesc.addressModeV = AddressMode::Repeat;
	samplerDesc.addressModeW = AddressMode::Repeat;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 8.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;
	m_sampler = m_device.createSampler(samplerDesc);

	// Create a texture
	m_texture = ResourceManager::loadTexture(RESOURCE_DIR "/fourareen2K_albedo.jpg", m_device, &m_textureView);
	if (!m_texture) {
		std::cerr << "Could not load texture!" << std::endl;
		return false;
	}
	std::cout << "Texture: " << m_texture << std::endl;
	std::cout << "Texture view: " << m_textureView << std::endl;

	// Load mesh data from OBJ file
	std::vector<VertexAttributes> vertexData;
	bool success = ResourceManager::loadGeometryFromObj(RESOURCE_DIR "/fourareen.obj", vertexData);
	if (!success) {
		std::cerr << "Could not load geometry!" << std::endl;
		return false;
	}

	// Create vertex buffer
	BufferDescriptor bufferDesc;
	bufferDesc.size = vertexData.size() * sizeof(VertexAttributes);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
	bufferDesc.mappedAtCreation = false;
	m_vertexBuffer = m_device.createBuffer(bufferDesc);
	m_queue.writeBuffer(m_vertexBuffer, 0, vertexData.data(), bufferDesc.size);

	m_vertexCount = static_cast<int>(vertexData.size());
	
	// Create uniform buffer
	bufferDesc.size = sizeof(MyUniforms);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	m_uniformBuffer = m_device.createBuffer(bufferDesc);

	// Upload the initial value of the uniforms
	m_uniforms.modelMatrix = mat4x4(1.0);
	m_uniforms.viewMatrix = glm::lookAt(vec3(-2.0f, -3.0f, 2.0f), vec3(0.0f), vec3(0, 0, 1));
	m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
	m_uniforms.time = 1.0f;
	m_uniforms.color = { 0.0f, 1.0f, 0.4f, 1.0f };
	m_queue.writeBuffer(m_uniformBuffer, 0, &m_uniforms, sizeof(MyUniforms));

	// Create a binding
	std::vector<BindGroupEntry> bindings(3);

	bindings[0].binding = 0;
	bindings[0].buffer = m_uniformBuffer;
	bindings[0].offset = 0;
	bindings[0].size = sizeof(MyUniforms);

	bindings[1].binding = 1;
	bindings[1].textureView = m_textureView;

	bindings[2].binding = 2;
	bindings[2].sampler = m_sampler;

	BindGroupDescriptor bindGroupDesc;
	bindGroupDesc.layout = bindGroupLayout;
	bindGroupDesc.entryCount = (uint32_t)bindings.size();
	bindGroupDesc.entries = bindings.data();
	m_bindGroup = m_device.createBindGroup(bindGroupDesc);

	// Setup GLFW callbacks
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, onWindowResize);
	glfwSetCursorPosCallback(m_window, onWindowMouseMove);
	glfwSetMouseButtonCallback(m_window, onWindowMouseButton);
	glfwSetScrollCallback(m_window, onWindowScroll);

	return true;
}

void Application::onFrame() {
	glfwPollEvents();
	updateDragInertia();

	// Update uniform buffer
	m_uniforms.time = static_cast<float>(glfwGetTime());
	m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, time), &m_uniforms.time, sizeof(MyUniforms::time));
	
	TextureView nextTexture = m_swapChain.getCurrentTextureView();
	if (!nextTexture) {
		std::cerr << "Cannot acquire next swap chain texture" << std::endl;
		return;
	}

	CommandEncoderDescriptor commandEncoderDesc;
	commandEncoderDesc.label = "Command Encoder";
	CommandEncoder encoder = m_device.createCommandEncoder(commandEncoderDesc);
	
	RenderPassDescriptor renderPassDesc{};

	RenderPassColorAttachment renderPassColorAttachment{};
	renderPassColorAttachment.view = nextTexture;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = LoadOp::Clear;
	renderPassColorAttachment.storeOp = StoreOp::Store;
	renderPassColorAttachment.clearValue = Color{ 0.05, 0.05, 0.05, 1.0 };
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	RenderPassDepthStencilAttachment depthStencilAttachment;
	depthStencilAttachment.view = m_depthTextureView;
	depthStencilAttachment.depthClearValue = 1.0f;
	depthStencilAttachment.depthLoadOp = LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = StoreOp::Store;
	depthStencilAttachment.depthReadOnly = false;
	depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
	depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
	depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
	depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
#endif
	depthStencilAttachment.stencilReadOnly = true;

	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

	renderPassDesc.timestampWriteCount = 0;
	renderPassDesc.timestampWrites = nullptr;
	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	renderPass.setPipeline(m_pipeline);

	renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(VertexAttributes));

	// Set binding group
	renderPass.setBindGroup(0, m_bindGroup, 0, nullptr);

	renderPass.draw(m_vertexCount, 1, 0, 0);

	renderPass.end();
	
	nextTexture.release();

	CommandBufferDescriptor cmdBufferDescriptor{};
	cmdBufferDescriptor.label = "Command buffer";
	CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	m_queue.submit(command);

	m_swapChain.present();

#ifdef WEBGPU_BACKEND_DAWN
	// Check for pending error callbacks
	m_device.tick();
#endif
}

void Application::onFinish() {
	m_vertexBuffer.destroy();
	m_vertexBuffer.release();

	m_textureView.release();
	m_texture.destroy();
	m_texture.release();

	m_depthTextureView.release();
	m_depthTexture.destroy();
	m_depthTexture.release();

	m_swapChain.release();
	m_queue.release();
	m_device.release();
	m_adapter.release();
	m_instance.release();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}

bool Application::isRunning() {
	return !glfwWindowShouldClose(m_window);
}

void Application::onResize() {
	buildSwapChain();
	buildDepthBuffer();

	float ratio = m_swapChainDesc.width / (float)m_swapChainDesc.height;
	m_uniforms.projectionMatrix = glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
	m_queue.writeBuffer(
		m_uniformBuffer,
		offsetof(MyUniforms, projectionMatrix),
		&m_uniforms.projectionMatrix,
		sizeof(MyUniforms::projectionMatrix)
	);
}

void Application::onMouseMove(double xpos, double ypos) {
	if (m_drag.active) {
		vec2 currentMouse = vec2(-(float)xpos, (float)ypos);
		vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity;
		m_cameraState.angles = m_drag.startCameraState.angles + delta;
		// Clamp to avoid going too far when orbitting up/down
		m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
		updateViewMatrix();

		// Inertia
		m_drag.velocity = delta - m_drag.previousDelta;
		m_drag.previousDelta = delta;
	}
}

void Application::onMouseButton(int button, int action, int modifiers) {
	// NB: This tells "I know we never use this variable" to the compiler
	// so that it does not issue warnings about it.
	(void)modifiers;

	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		switch (action) {
		case GLFW_PRESS:
			m_drag.active = true;
			double xpos, ypos;
			glfwGetCursorPos(m_window, &xpos, &ypos);
			m_drag.startMouse = vec2(-(float)xpos, (float)ypos);
			m_drag.startCameraState = m_cameraState;
			break;
		case GLFW_RELEASE:
			m_drag.active = false;
			break;
		}
	}
}

void Application::onScroll([[maybe_unused]] double xoffset, double yoffset) {
	m_cameraState.zoom += m_drag.scrollSensitivity * (float)yoffset;
	m_cameraState.zoom = glm::clamp(m_cameraState.zoom, -2.0f, 2.0f);
	updateViewMatrix();
}

/* ***** Private methods ***** */

void Application::buildSwapChain() {
	int width, height;
	glfwGetFramebufferSize(m_window, &width, &height);

	// Destroy previously allocated swap chain
	if (m_swapChain != nullptr) {
		m_swapChain.release();
	}

	std::cout << "Creating swapchain..." << std::endl;
#ifdef WEBGPU_BACKEND_WGPU
	m_swapChainFormat = m_surface.getPreferredFormat(m_adapter);
#else
	m_swapChainFormat = TextureFormat::BGRA8Unorm;
#endif
	m_swapChainDesc.width = static_cast<uint32_t>(width);
	m_swapChainDesc.height = static_cast<uint32_t>(height);
	m_swapChainDesc.usage = TextureUsage::RenderAttachment;
	m_swapChainDesc.format = m_swapChainFormat;
	m_swapChainDesc.presentMode = PresentMode::Fifo;
	m_swapChain = m_device.createSwapChain(m_surface, m_swapChainDesc);
	std::cout << "Swapchain: " << m_swapChain << std::endl;
}

void Application::buildDepthBuffer() {
	// Destroy previously allocated texture
	if (m_depthTexture != nullptr) {
		m_depthTextureView.release();
		m_depthTexture.destroy();
		m_depthTexture.release();
	}

	// Create the depth texture
	TextureDescriptor depthTextureDesc;
	depthTextureDesc.dimension = TextureDimension::_2D;
	depthTextureDesc.format = m_depthTextureFormat;
	depthTextureDesc.mipLevelCount = 1;
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.size = { m_swapChainDesc.width, m_swapChainDesc.height, 1};
	depthTextureDesc.usage = TextureUsage::RenderAttachment;
	depthTextureDesc.viewFormatCount = 1;
	depthTextureDesc.viewFormats = (WGPUTextureFormat*)&m_depthTextureFormat;
	m_depthTexture = m_device.createTexture(depthTextureDesc);
	std::cout << "Depth texture: " << m_depthTexture << std::endl;

	// Create the view of the depth texture manipulated by the rasterizer
	TextureViewDescriptor depthTextureViewDesc;
	depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
	depthTextureViewDesc.baseArrayLayer = 0;
	depthTextureViewDesc.arrayLayerCount = 1;
	depthTextureViewDesc.baseMipLevel = 0;
	depthTextureViewDesc.mipLevelCount = 1;
	depthTextureViewDesc.dimension = TextureViewDimension::_2D;
	depthTextureViewDesc.format = m_depthTextureFormat;
	m_depthTextureView = m_depthTexture.createView(depthTextureViewDesc);
	std::cout << "Depth texture view: " << m_depthTextureView << std::endl;
}

void Application::updateViewMatrix() {
	float cx = cos(m_cameraState.angles.x);
	float sx = sin(m_cameraState.angles.x);
	float cy = cos(m_cameraState.angles.y);
	float sy = sin(m_cameraState.angles.y);
	vec3 position = vec3(cx * cy, sx * cy, sy) * std::exp(-m_cameraState.zoom);
	m_uniforms.viewMatrix = glm::lookAt(position, vec3(0.0f), vec3(0, 0, 1));
	m_queue.writeBuffer(
		m_uniformBuffer,
		offsetof(MyUniforms, viewMatrix),
		&m_uniforms.viewMatrix,
		sizeof(MyUniforms::viewMatrix)
	);
}

void Application::updateDragInertia() {
	constexpr float eps = 1e-4f;
	// Apply inertia only when the user released the click.
	if (!m_drag.active) {
		// Avoid updating the matrix when the velocity is no longer noticeable
		if (std::abs(m_drag.velocity.x) < eps && std::abs(m_drag.velocity.y) < eps) {
			return;
		}
		m_cameraState.angles += m_drag.velocity;
		m_cameraState.angles.y = glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
		// Dampen the velocity so that it decreases exponentially and stops
		// after a few frames.
		m_drag.velocity *= m_drag.intertia;
		updateViewMatrix();
	}
}
