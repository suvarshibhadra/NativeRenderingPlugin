#pragma once

#include <map>

#include "Unity/IUnityGraphics.h"
#include <vector>

#define VK_NO_PROTOTYPES // structs will get defined but methods wont
#include <d3d11.h>

#include "Unity/IUnityGraphicsVulkan.h"

#if defined (_WIN32)
#include <windows.h>
//#include <vulkan/vulkan_win32.h>
#elif defined(__ANDROID__)
//#include <vulkan/vulkan_android.h>
#endif

class VulkanExternalImageHandler {
public:

	// Constructors
	VulkanExternalImageHandler(ID3D11Device* d3d11Device);
	~VulkanExternalImageHandler() = default;
	
	// Vulkan Device Creation
    static void LoadVulkanSharedLibrary();
    void CreateVulkanInstance();
    void LoadVulkanFnPtrs();
    void CreateVulkanDevice();

    // Create Vulkan Image and Export Shared Handle
	void DX11Handle_VulkanCreatedExternalImage(unsigned int width, unsigned int height, ID3D11Texture2D** handle);

    // Create DX11 Image, Share with Vulkan
    void DX11Handle_VulkanShared_ExternalImage(unsigned int width, unsigned int height, ID3D11ShaderResourceView** outShaderResourceView);
    
    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

private:
    // DX11 Device Details
    int m_unitySelectedDeviceId = -1;
    ID3D11Device* m_d3d11Device = nullptr;

    // Vulkan Device Details
    VkInstance m_vkInstance;
    VkPhysicalDevice m_vkPhysicalDevice;
    VkDevice m_vkDevice;
    VkDebugUtilsMessengerEXT m_DebugUtilsMessenger;

};

// Create a graphics API implementation instance for the given API type.
VulkanExternalImageHandler* CreateRenderAPI_VulkanDX11();

