#include "pch.h"
#include "texture.h"

Texture Texture::Create(Texture::CreateInfo& info)
{
	return Texture(info);
}

void Texture::cleanup()
{
	APP_LOG_INFO("Destroying texture ({})", m_name);

	m_image.cleanup(m_device->getLogical());
	vkDestroySampler(m_device->getLogical(), m_descriptor.sampler, nullptr);
}

Texture::Texture(Texture::CreateInfo& info)
{
	APP_LOG_INFO("Creating texture ({})", info.name);

	m_device = info.pDevice;
	m_name   = info.name;

	// Create image
	Image::CreateInfo imgCreateInfo{};
	imgCreateInfo.width      = info.extent.width;
	imgCreateInfo.height     = info.extent.height;
	imgCreateInfo.mipLevels  = 1;
	imgCreateInfo.layerCount = 1;
	imgCreateInfo.numSamples = VK_SAMPLE_COUNT_1_BIT;
	imgCreateInfo.tiling     = VK_IMAGE_TILING_OPTIMAL;
	imgCreateInfo.usage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imgCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	imgCreateInfo.device     = info.pDevice;
	imgCreateInfo.name       = info.name;

	imgCreateInfo.format = info.pDevice->findSupportedFormat(
		{ VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

	m_image = Image::CreateImage(imgCreateInfo);
	m_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Setup image view
	Image::ImageViewSetupInfo viewSetupInfo{};
	viewSetupInfo.format      = imgCreateInfo.format;
	viewSetupInfo.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	viewSetupInfo.mipLevels   = 1;
	viewSetupInfo.layerCount  = 1;
	viewSetupInfo.device      = info.pDevice;

	Image::SetupImageView(m_image, viewSetupInfo);
	m_descriptor.imageView = m_image.view;

	// Create sampler
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(info.pDevice->getPhysical(), &properties);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter               = VK_FILTER_LINEAR;
	samplerInfo.minFilter               = VK_FILTER_LINEAR;
	samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable        = VK_TRUE;
	samplerInfo.maxAnisotropy           = properties.limits.maxSamplerAnisotropy;
	samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable           = VK_FALSE;
	samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.minLod                  = 0.0f;
	samplerInfo.maxLod                  = 1.0f;
	samplerInfo.mipLodBias              = 0.0f;

	if (vkCreateSampler(info.pDevice->getLogical(), &samplerInfo, nullptr, &m_descriptor.sampler) != VK_SUCCESS)
	{
		APP_LOG_CRITICAL("Failed to create sampler ({})", m_name);
		throw;
	}
}