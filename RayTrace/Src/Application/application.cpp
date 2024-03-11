#include "pch.h"

#include "application.h"

void Application::init(Application::Settings& settings)
{
	// Store settings
	m_settings = settings;

	// Initialize logger to info level
	Logger::init(LogLevel::INFO);

	// CPU Raytracing
	if (m_settings.cpuRaytracing)
	{
		m_cpuRaytracer.init(settings.windowWidth,settings.windowHeight);
		return;
	}

	// Window
	m_window.init(settings.windowWidth, settings.windowHeight);

	// System Context
	m_context.init(m_window, m_settings.gpuRaytracing);

	// Swapchain initialization
	Swapchain::CreateInfo swapchainCreateInfo{};
	swapchainCreateInfo.device         = &m_context.getDevice();
	swapchainCreateInfo.window         = &m_window;
	swapchainCreateInfo.surface        = &m_context.getSurface();
	swapchainCreateInfo.framesInFlight = m_settings.framesInFlight;
	swapchainCreateInfo.vSync          = settings.vSync;
	swapchainCreateInfo.msaa           = false;
	m_swapchain.init(swapchainCreateInfo);

	// Offscreen render
	setupOffscreenRender();

	// Render passes
	createRenderPasses();

	// Framebuffers
	createFramebuffers();

	// Command system
	m_commandSystem.init(m_context.getDevice(), m_settings.framesInFlight);

	// Uniform buffers
	Buffer::CreateInfo uboInfo{};
	uboInfo.device        = &m_context.getDevice();
	uboInfo.commandSystem = &m_commandSystem;
	uboInfo.dataSize      = sizeof(GlobalUniform);

	for (uint8_t i = 0; i < m_settings.framesInFlight; i++)
	{
		// Set name
		char buffer[128];
		sprintf(buffer, "Global Uniform Buffer %d", i);
		uboInfo.name = buffer;

		m_uniformBuffers.push_back(Buffer::CreateUniformBuffer(uboInfo));
	}

	// Load scene
	loadScene();

	// Descriptor Sets
	createDescriptorSets();

	// Camera
	Camera::CreateInfo cameraInfo{};
	cameraInfo.position     = { 0.0f, 0.0f, 6.0f };
	cameraInfo.fov          = 45.0f;
	cameraInfo.nearPlane    = 0.1f;
	cameraInfo.farPlane     = 1000.0f;
	cameraInfo.sensitivity  = 0.5f;
	cameraInfo.windowHeight = m_swapchain.getExtent().height;
	cameraInfo.windowWidth  = m_swapchain.getExtent().width;
	m_camera.init(cameraInfo);

	// Pipeline
	createPipelines();

	// ImGui
	Device device = m_context.getDevice(); //Reduce function calls

	ImGui_ImplVulkan_InitInfo guiInitInfo{};
	guiInitInfo.Instance       = m_context.getInstance();
	guiInitInfo.PhysicalDevice = device.getPhysical();
	guiInitInfo.Device         = device.getLogical();
	guiInitInfo.QueueFamily    = device.getIndices().graphicsFamily.value(); //Graphics or Present?
	guiInitInfo.Queue          = device.getGraphicsQueue();
	guiInitInfo.DescriptorPool = m_descriptorPool.getImguiPool(); //Make separate descriptor pool class? Ex. ImGuiDescriptorPool m_imguiDesPool
	guiInitInfo.RenderPass     = m_renderPasses[RenderPass::POST].renderPass;
	guiInitInfo.ImageCount     = m_swapchain.getImageCount();
	guiInitInfo.MinImageCount  = 2;
	guiInitInfo.MSAASamples    = m_swapchain.getMSAASampleCount();
	m_gui.init(guiInitInfo, m_window);
}

void Application::run()
{
	if (m_settings.cpuRaytracing)
	{
		m_cpuRaytracer.render();
		m_cpuRaytracer.cleanup();
		return;
	}

	// Setup rendering context
	Renderer::CreateInfo rendererInfo{};
	rendererInfo.pSwapchain               = &m_swapchain;
	rendererInfo.pCommandSystem           = &m_commandSystem;
	rendererInfo.pCamera                  = &m_camera;
	rendererInfo.pRenderPasses            = m_renderPasses.data();
	rendererInfo.pPipelines               = m_pipelines.data();
	rendererInfo.pOffscreenDescriptorSets = m_offscreenDescriptorSets.data();
	rendererInfo.pPostDescriptorSets      = m_postDescriptorSets.data();
	rendererInfo.pUniformBuffers          = m_uniformBuffers.data();
	rendererInfo.framesInFlight           = m_settings.framesInFlight;
	rendererInfo.pGui                     = &m_gui;
	rendererInfo.pOffscreenFramebuffer    = &m_offscreenFramebuffer;
	rendererInfo.pPostFramebuffers        = m_postFramebuffers.data();

	// Create renderer
	Renderer renderer(rendererInfo);

	Logger::changeLogLevel(LogLevel::TRACE);
	APP_LOG_INFO("Starting main render loop");

	// Run until the window is closed
	while (!m_window.isWindowClosed())
	{
		pollEvents();

		if (m_window.isWindowMinimized())
			continue;
		
		m_camera.updatePosition();
		m_scene.onUpdate(renderer);
	}

	APP_LOG_INFO("Main render loop ended");
	Logger::changeLogLevel(LogLevel::INFO);

	// Cleanup
	m_context.getDevice().waitForGPU();
	cleanup();
}

void Application::createRenderPasses()
{
	APP_LOG_INFO("Creating render passes");

	// Create render pass builder
	auto builder = RenderPass::Builder(m_context.getDevice());

	// Offscreen - MAIN
	{
		builder.addColorAttachment(
			m_offscreenColorTexture.getImage().format,
			m_offscreenColorTexture.getImage().numSamples,
			VK_IMAGE_LAYOUT_UNDEFINED,
			m_offscreenColorTexture.getDescriptor().imageLayout,
			{ {0.0f, 0.0f, 0.0f, 1.0f} });

		builder.addDepthAttachment(
			m_offscreenDepthBuffer.format,
			m_offscreenDepthBuffer.image.numSamples,
			VK_IMAGE_LAYOUT_UNDEFINED,
			{ 1.0f, 0 });

		m_renderPasses.push_back(builder.buildPass("Main Render Pass"));
	}

	// Post - POST
	{
		builder.reset();

		builder.addColorAttachment(
			m_swapchain.getFormat(),
			m_swapchain.getMSAASampleCount(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			{ {0.0f, 0.0f, 0.0f, 1.0f} },
			true);

		builder.addDepthAttachment(
			m_swapchain.getDepthFormat(),
			m_swapchain.getMSAASampleCount(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			{ 1.0f, 0 });

		m_renderPasses.push_back(builder.buildPass("Post Render Pass"));
	}
}

void Application::createPipelines()
{
	APP_LOG_INFO("Creating pipelines");

	// Create a pipeline builder
	auto builder = Pipeline::Builder(m_context.getDevice());

	// Offscreen
	{
		builder.addGraphicsBase();

		builder.disableFaceCulling();

		builder.linkRenderPass(m_renderPasses[RenderPass::MAIN]);
		builder.linkDescriptorSetLayout(m_offscreenDescriptorLayout);
		builder.linkPushConstants(sizeof(MeshPushConstants));

		// Lighting shaders
		RasterShaderSet lightingShaders("../../Shaders/lighting_vert.spv", "../../Shaders/lighting_frag.spv", m_context.getDevice());
		builder.linkShaders(lightingShaders);

		// Build graphics pipeline - LIGHTING
		m_pipelines.push_back(builder.buildPipeline(Pipeline::LIGHTING, "Lighting Pipeline"));

		// Flat shaders
		RasterShaderSet flatShaders("../../Shaders/flat_vert.spv", "../../Shaders/flat_frag.spv", m_context.getDevice());
		builder.linkShaders(flatShaders);

		// Build graphics pipeline - FLAT
		m_pipelines.push_back(builder.buildPipeline(Pipeline::FLAT, "Flat Pipeline"));

		// Cleanup shader set
		lightingShaders.cleanup();
		flatShaders.cleanup();
	}

	// Post
	{
		builder.reset();
		builder.addGraphicsBase();

		builder.disableFaceCulling();
		builder.disableDepthTesting();

		builder.linkRenderPass(m_renderPasses[RenderPass::POST]);
		builder.linkDescriptorSetLayout(m_postDescriptorLayout);

		// Post shaders
		RasterShaderSet postShaders("../../Shaders/post_vert.spv", "../../Shaders/post_frag.spv", m_context.getDevice());
		builder.linkShaders(postShaders);

		// Build graphics pipeline - POST
		m_pipelines.push_back(builder.buildPipeline(Pipeline::POST, "Post Pipeline"));

		// Cleanup shader set
		postShaders.cleanup();
	}
}

void Application::createDescriptorSets()
{
	// Initialize descriptor pool
	m_descriptorPool.init(m_context.getDevice(), m_settings.framesInFlight, 2);

	// Create descriptor set layout builder
	auto layoutBuilder = DescriptorSetLayout::Builder(m_context.getDevice());

	// Offscreen pass
	{
		// Add a uniform buffer to the global binding
		layoutBuilder.addBinding(
			(uint32_t)SceneBinding::GLOBAL,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

		// Add a storage buffer for the scene object materials
		layoutBuilder.addBinding(
			(uint32_t)SceneBinding::MATERIAL,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
			VK_SHADER_STAGE_FRAGMENT_BIT);

		m_offscreenDescriptorLayout = layoutBuilder.buildLayout("Offscreen Descriptor Set Layout");
	}

	// Post pass
	{
		layoutBuilder.reset();

		// Add a sampler for the incoming image - binding 0
		layoutBuilder.addBinding(
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
			VK_SHADER_STAGE_FRAGMENT_BIT);

		m_postDescriptorLayout = layoutBuilder.buildLayout("Post Descriptor Set Layout");
	}
	
	// Allocate and update descriptors set for each frame in flight
	for (uint8_t i = 0; i < m_settings.framesInFlight; i++)
	{
		// Allocate sets
		m_offscreenDescriptorSets.push_back(m_descriptorPool.allocateDescriptorSet(m_offscreenDescriptorLayout));
		m_postDescriptorSets.push_back(m_descriptorPool.allocateDescriptorSet(m_postDescriptorLayout));

		// Update offscreen set
		m_offscreenDescriptorSets[i].addBufferWrite(m_uniformBuffers[i], BufferType::UNIFORM, 0, (uint32_t)SceneBinding::GLOBAL);
		m_offscreenDescriptorSets[i].addBufferWrite(m_materialDescriptionBuffer, BufferType::STORAGE, 0, (uint32_t)SceneBinding::MATERIAL);
		m_offscreenDescriptorSets[i].update(m_context.getDevice());

		// Update post set
		m_postDescriptorSets[i].addImageWrite(m_offscreenColorTexture.getDescriptor(), 0);
		m_postDescriptorSets[i].update(m_context.getDevice());
	}
}

void Application::createFramebuffers()
{
	// Create offscreen framebuffer
	{
		std::array<VkImageView, 2> offscreenAttachments = {
			m_offscreenColorTexture.getImage().view,
			m_offscreenDepthBuffer.image.view
		};

		Framebuffer::CreateInfo info{};
		info.pRenderPass    = &m_renderPasses[RenderPass::MAIN];
		info.pDevice        = &m_context.getDevice();
		info.pAttachments   = offscreenAttachments.data();
		info.numAttachments = static_cast<uint32_t>(offscreenAttachments.size());
		info.extent         = m_swapchain.getExtent();
		info.name           = "Offscreen Framebuffer";

		m_offscreenFramebuffer = Framebuffer(info);
	}

	// Create post framebuffers
	{
		m_postFramebuffers.resize(m_settings.framesInFlight);

		std::array<VkImageView, 2> postAttachments;

		Framebuffer::CreateInfo info{};
		info.pRenderPass    = &m_renderPasses[RenderPass::POST];
		info.pDevice        = &m_context.getDevice();
		info.pAttachments   = postAttachments.data();
		info.numAttachments = static_cast<uint32_t>(postAttachments.size());
		info.extent         = m_swapchain.getExtent();

		for (uint8_t i = 0; i < m_postFramebuffers.size(); i++)
		{
			// Set name
			char name[128];
			sprintf(name, "Post Framebuffer %d", i);
			info.name = name;

			postAttachments[0] = m_swapchain.getImage(i).view;
			postAttachments[1] = m_swapchain.getDepthBuffer().image.view;

			m_postFramebuffers[i] = Framebuffer(info);
		}
	}
}

void Application::setupOffscreenRender()
{
	// Color texture
	Texture::CreateInfo textureInfo{};
	textureInfo.pDevice     = &m_context.getDevice();
	textureInfo.extent      = m_swapchain.getExtent();
	textureInfo.name        = "Offscreen Texture";
	m_offscreenColorTexture = Texture::Create(textureInfo);

	// Create depth buffer
	m_offscreenDepthBuffer = DepthBuffer(
		m_context.getDevice(), 
		m_swapchain.getExtent(), 
		m_offscreenColorTexture.getImage().numSamples, 
		"Offscreen Depth Buffer");
}

void Application::resetOffscreenRender()
{
	// Destroy old framebuffers
	for (auto& framebuffer : m_postFramebuffers)
		framebuffer.cleanup();
	m_offscreenFramebuffer.cleanup();

	// Destroy attachments
	m_offscreenColorTexture.cleanup();
	m_offscreenDepthBuffer.cleanup();

	// Reset texture and depth buffer
	setupOffscreenRender();

	// Update descriptor sets
	for (auto& set : m_postDescriptorSets)
	{
		// Update post set
		set.addImageWrite(m_offscreenColorTexture.getDescriptor(), 0);
		set.update(m_context.getDevice());
	}

	// Make new framebuffers
	createFramebuffers();
}

void Application::loadScene()
{
	APP_LOG_INFO("Loading scene");

	// Load scene
	ModelLoader loader(m_context.getDevice(), m_commandSystem);
	m_scene.onLoad(loader);

	// Create object description buffer
	std::vector<ObjectDescription> objectDescriptions = loader.getObjectDescriptions();

	Buffer::CreateInfo createInfo{};
	createInfo.device           = &m_context.getDevice();
	createInfo.commandSystem    = &m_commandSystem;
	createInfo.data             = objectDescriptions.data();
	createInfo.dataSize         = sizeof(ObjectDescription) * objectDescriptions.size();
	createInfo.dataCount        = static_cast<uint32_t>(objectDescriptions.size());
	createInfo.name             = "Object Description Storage Buffer";

	m_materialDescriptionBuffer = Buffer::CreateStorageBuffer(createInfo);

	// Create acceleration structure
	if (m_context.getDevice().isRtxEnabled())
		m_accelerationStructure.init(loader.getModelInformation(), loader.getInstances(), m_context.getDevice(), m_commandSystem);
}

void Application::pollEvents()
{
	glfwPollEvents();

	// This is used to avoid processing more than one window resize per frame
	bool processedWindowResize = false;

	// Loop over all events that occurred
	for (std::unique_ptr<Event>& event : EventDispatcher::GetEventQueue())
	{
		switch (event->getType())
		{
			case EventType::WINDOW_RESIZE:
			{
				if (processedWindowResize)
					break;
				processedWindowResize = true;

				auto windowResizeEvent = dynamic_cast<WindowResizeEvent*>(event.get());
				APP_LOG_TRACE(windowResizeEvent->eventString());

				m_swapchain.onWindowResize(*windowResizeEvent);
				m_camera.onWindowResize(*windowResizeEvent);
				resetOffscreenRender();
				break;
			}

			case EventType::WINDOW_MINIMIZED:
			{
				auto minimizedEvent = dynamic_cast<WindowMinimizedEvent*>(event.get());
				APP_LOG_TRACE(minimizedEvent->eventString());

				m_window.onWindowMinimized();
				break;
			}

			case EventType::MOUSE_CLICK:
			{
				auto mouseClickEvent = dynamic_cast<MouseClickEvent*>(event.get());
				APP_LOG_TRACE(mouseClickEvent->eventString());

				m_camera.onMouseClick(*mouseClickEvent);
				break;
			}

			case EventType::MOUSE_RELEASE:
			{
				auto mouseReleaseEvent = dynamic_cast<MouseReleaseEvent*>(event.get());
				APP_LOG_TRACE(mouseReleaseEvent->eventString());

				m_camera.onMouseRelease(*mouseReleaseEvent);
				break;
			}

			case EventType::MOUSE_MOVE:
			{
				auto mouseMoveEvent = dynamic_cast<MouseMoveEvent*>(event.get());
				// APP_LOG_TRACE(mouseMoveEvent->eventString());

				m_camera.onMouseMove(*mouseMoveEvent);
				break;
			}

			case EventType::KEY_PRESS:
			{
				auto keyPressEvent = dynamic_cast<KeyPressEvent*>(event.get());
				APP_LOG_TRACE(keyPressEvent->eventString());

				m_camera.onKeyPress(*keyPressEvent);
				break;
			}

			case EventType::KEY_RELEASE:
			{
				auto keyReleaseEvent = dynamic_cast<KeyReleaseEvent*>(event.get());
				APP_LOG_TRACE(keyReleaseEvent->eventString());

				m_camera.onKeyRelease(*keyReleaseEvent);
				break;
			}
		}
	}

	// Cleanup event queue
	EventDispatcher::GetEventQueue().clear();
}

void Application::cleanup()
{
	// Scene
	m_scene.onUnload();

	// ImGui
	m_gui.cleanup();

	// Acceleration Structure
	if (m_context.getDevice().isRtxEnabled())
		m_accelerationStructure.cleanup();

	// Framebuffers
	for (auto& framebuffer : m_postFramebuffers)
		framebuffer.cleanup();
	m_offscreenFramebuffer.cleanup();

	// Pipelines
	for (auto& pipeline : m_pipelines)
		pipeline.cleanup(m_context.getDevice());

	// Command System
	m_commandSystem.cleanup();
	
	// Descriptor stuff
	m_offscreenDescriptorLayout.cleanup(m_context.getDevice());
	m_postDescriptorLayout.cleanup(m_context.getDevice());
	m_descriptorPool.cleanup();

	// Buffers
	for (auto& buffer : m_uniformBuffers)
		buffer.cleanup();
	m_materialDescriptionBuffer.cleanup();

	// Render passes
	for (auto& renderPass : m_renderPasses)
		renderPass.cleanup(m_context.getDevice());

	// Offscreen stuff
	m_offscreenColorTexture.cleanup();
	m_offscreenDepthBuffer.cleanup();

	// Swapchain
	m_swapchain.cleanup();

	// System stuff
	m_context.cleanup();
	m_window.cleanup();

	APP_LOG_INFO("Application has been successfully cleaned up");
}
