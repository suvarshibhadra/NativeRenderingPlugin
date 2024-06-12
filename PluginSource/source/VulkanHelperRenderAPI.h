#pragma once

#include <map>

#include "Unity/IUnityGraphics.h"

#include <stddef.h>
#include <vector>

#define VK_NO_PROTOTYPES // structs will get defined but methods wont
#include "Unity/IUnityGraphicsVulkan.h"

#if defined (_WIN32)
#include <windows.h>
//#include <vulkan/vulkan_win32.h>
#elif defined(__ANDROID__)
//#include <vulkan/vulkan_android.h>
#endif

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory deviceMemory;
    void* mapped;
    VkDeviceSize sizeInBytes;
    VkDeviceSize deviceMemorySize;
    VkMemoryPropertyFlags deviceMemoryFlags;
};

class RenderAPI_VulkanDX11 {
public:
    // Constructors
	RenderAPI_VulkanDX11();
	virtual ~RenderAPI_VulkanDX11() = default;

    void LoadVulkanSharedLibrary();
    void CreateVulkanInstance();
    void LoadVulkanFnPtrs();
    void CreateVulkanDevice();
    void CreateVulkanImage(unsigned int width, unsigned int height, HANDLE* handle);
    void SetUnitySelectedDeviceId(int unitySelectedDeviceId);

    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

private:
    typedef std::vector<VulkanBuffer> VulkanBuffers;
    typedef std::map<unsigned long long, VulkanBuffers> DeleteQueue;

    int m_unitySelectedDeviceId = -1;
    IUnityGraphicsVulkan* m_UnityVulkan;
    UnityVulkanInstance m_Instance;
    VulkanBuffer m_TextureStagingBuffer;
    VulkanBuffer m_VertexStagingBuffer;
    std::map<unsigned long long, VulkanBuffers> m_DeleteQueue;
    VkPipelineLayout m_TrianglePipelineLayout;
    VkPipeline m_TrianglePipeline;
    VkRenderPass m_TrianglePipelineRenderPass;
};

// Create a graphics API implementation instance for the given API type.
RenderAPI_VulkanDX11* CreateRenderAPI_VulkanDX11();

