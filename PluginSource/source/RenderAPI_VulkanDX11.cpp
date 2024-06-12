#include <cassert>
#include <iostream>

#include "RenderAPI.h"
#include "PlatformBase.h"
#include "VulkanHelperRenderAPI.h"
#include "gl3w/gl3w.h"

#if SUPPORT_VULKAN

#include <string.h>
#include <map>
#include <vector>
#include <math.h>

// This plugin does not link to the Vulkan loader, easier to support multiple APIs and systems that don't have Vulkan support
#include "Unity/IUnityGraphicsVulkan.h"

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#endif


#define UNITY_USED_VULKAN_API_FUNCTIONS(apply) \
    apply(vkCreateInstance); \
    apply(vkEnumeratePhysicalDevices); \
	apply(vkGetPhysicalDeviceFeatures); \
	apply(vkGetPhysicalDeviceProperties); \
    apply(vkGetPhysicalDeviceQueueFamilyProperties); \
    apply(vkEnumerateDeviceExtensionProperties); \
    apply(vkCreateDevice); \
    apply(vkGetPhysicalDeviceImageFormatProperties2); \
    apply(vkCreateImage); \
    apply(vkGetImageMemoryRequirements); \
	apply(vkAllocateMemory); \
	apply(vkBindImageMemory); \
    apply(vkGetMemoryWin32HandleKHR); \
    apply(vkCmdBeginRenderPass); \
    apply(vkCreateBuffer); \
    apply(vkGetPhysicalDeviceMemoryProperties); \
    apply(vkGetBufferMemoryRequirements); \
    apply(vkMapMemory); \
    apply(vkBindBufferMemory); \
    apply(vkDestroyBuffer); \
    apply(vkFreeMemory); \
    apply(vkUnmapMemory); \
    apply(vkQueueWaitIdle); \
    apply(vkDeviceWaitIdle); \
    apply(vkCmdCopyBufferToImage); \
    apply(vkFlushMappedMemoryRanges); \
    apply(vkCreatePipelineLayout); \
    apply(vkCreateShaderModule); \
    apply(vkDestroyShaderModule); \
    apply(vkCreateGraphicsPipelines); \
    apply(vkCmdBindPipeline); \
    apply(vkCmdDraw); \
    apply(vkCmdPushConstants); \
    apply(vkCmdBindVertexBuffers); \
    apply(vkDestroyPipeline); \
    apply(vkDestroyPipelineLayout);

#define VULKAN_DEFINE_API_FUNCPTR(func) static PFN_##func func
VULKAN_DEFINE_API_FUNCPTR(vkGetInstanceProcAddr);
UNITY_USED_VULKAN_API_FUNCTIONS(VULKAN_DEFINE_API_FUNCPTR);
#undef VULKAN_DEFINE_API_FUNCPTR

static void LoadVulkanAPI(PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkInstance instance)
{
    if (!vkGetInstanceProcAddr && getInstanceProcAddr)
        vkGetInstanceProcAddr = getInstanceProcAddr;

#define LOAD_VULKAN_FUNC(fn) if (!fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn)
    UNITY_USED_VULKAN_API_FUNCTIONS(LOAD_VULKAN_FUNC);
#undef LOAD_VULKAN_FUNC
}

//PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
void RenderAPI_VulkanDX11::LoadVulkanSharedLibrary()
{
#if defined _WIN32
    HMODULE vulkan_library = LoadLibrary("vulkan-1.dll");
    if (vulkan_library) {
        vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkan_library, "vkGetInstanceProcAddr");
    }
    
#elif defined __LINUX
    vulkan_library = dlopen("libvulkan.so.1", RTLD_NOW);
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vulkan_library, "vkGetInstanceProcAddr");
#endif
}

static VkInstance s_vkInstance;
static VkPhysicalDevice s_vkPhysicalDevice;
static VkDevice s_vkDevice;

void RenderAPI_VulkanDX11::CreateVulkanInstance()
{
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(nullptr, "vkCreateInstance");

	VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Unity Vulkan Plugin";
    appInfo.pEngineName = "";
    //TODO Maybe source this from vkEnumerateInstanceVersion 
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pNext = NULL;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    // Specify Instance Extensions
    std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_get_physical_device_properties2", "VK_KHR_external_memory_capabilities", "VK_KHR_external_semaphore_capabilities", "VK_KHR_external_fence_capabilities" };
#if defined(_WIN32)
    instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif

    static bool debugUtils = false;
    if (instanceExtensions.size() > 0) {
        if (debugUtils) {
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }

    // TODO Check if requested instance extensions are actually present via vkEnumerateInstanceExtensionProperties
    // TODO Check if requested instance layers are actually present via vkEnumerateInstanceLayerProperties

    // Specify instance layers
    // The VK_LAYER_KHRONOS_validation contains all current validation functionality.
    // Note that on Android this layer requires at least NDK r20
    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
    instanceCreateInfo.enabledLayerCount = 1;

    // Create the instance
    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &s_vkInstance);
    printf("VkResult : %d", result);
}

void RenderAPI_VulkanDX11::LoadVulkanFnPtrs()
{
}

bool EnumerateAvailablePhysicalDevices(VkInstance instance,
    std::vector<VkPhysicalDevice>& available_devices) {

    uint32_t devices_count = 0;
    VkResult result = VK_SUCCESS;

    result = vkEnumeratePhysicalDevices(instance, &devices_count, nullptr);
    if ((result != VK_SUCCESS) ||
        (devices_count == 0)) {
        std::cout << "Could not get the number of available physical devices." << std::endl;
        return false;
    }

    available_devices.resize(devices_count);
    result = vkEnumeratePhysicalDevices(instance, &devices_count, available_devices.data());
    if ((result != VK_SUCCESS) ||
        (devices_count == 0)) {
        std::cout << "Could not enumerate physical devices." << std::endl;
        return false;
    }

    return true;
}

void RenderAPI_VulkanDX11::CreateVulkanDevice()
{
    VkPhysicalDevice selectedPhysicalDevice = {};
    int gfxQueueFamilyIndexOfSelectedDevice = -1;

    std::vector<VkPhysicalDevice> available_devices = {};
    EnumerateAvailablePhysicalDevices(s_vkInstance, available_devices);

    std::vector<const char *> desiredDeviceExtensions = {"VK_KHR_external_memory"};

#if defined(_WIN32)
    desiredDeviceExtensions.emplace_back("VK_KHR_external_memory_win32");
    desiredDeviceExtensions.emplace_back("VK_KHR_external_semaphore_win32");
    desiredDeviceExtensions.emplace_back("VK_KHR_external_fence_win32");
#endif


    for (auto& physicalDevice : available_devices) {

        // Obtain device features and properties
        VkPhysicalDeviceFeatures device_features;
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceFeatures(physicalDevice, &device_features);
        vkGetPhysicalDeviceProperties(physicalDevice, &device_properties);

        uint32_t queueFamilyCount;
        std::vector<VkQueueFamilyProperties> queueFamilyProperties = {};
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        assert(queueFamilyCount > 0);
        queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

        // Selecting queueFamilyIdx with Graphics Capability 
        int queueFamilyIdxWithGraphicsCapability = -1;
        // Iterate through all the queueFamilies (and check the the queueFlags of their QueueFamilyProperties)
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
            if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)	{
                // Found it!
                queueFamilyIdxWithGraphicsCapability = i;
            }
        }

        if (queueFamilyIdxWithGraphicsCapability == -1) continue;

        // Get list of supported extensions
        uint32_t extCount = 0;
        std::vector<std::string> supportedExtensions = {};
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
        if (extCount > 0) {
            std::vector<VkExtensionProperties> extensions(extCount);
            if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
            {
                for (auto& ext : extensions) {
                    supportedExtensions.emplace_back(ext.extensionName);
                }
            }
        }

        // Check available extensions against device extensions
        for (auto& extension : desiredDeviceExtensions) {
            if (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) == supportedExtensions.end()){
				std::cout << "Extension named '" << extension << "' is not supported by a physical device." << std::endl;
                continue;
            }
        }

        // Should match 
        if (m_unitySelectedDeviceId != -1) {
            // This is the Device that Unity Selected
            if (device_properties.deviceID != m_unitySelectedDeviceId) {
                std::cout << "Not the physical device selected by Unity will not be able to share external memory." << std::endl;
                continue;
            }
            std::cout << "Matches with Unity Selection" << std::endl;
        }

        // Reaching here means passing all the VkPhysicalDevice checks so select this device
        selectedPhysicalDevice = physicalDevice;
        gfxQueueFamilyIndexOfSelectedDevice = queueFamilyIdxWithGraphicsCapability;
        break;
    }

		// Requested Queues
		float defaultQueuePriority = 0.0f;
		std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = gfxQueueFamilyIndexOfSelectedDevice;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &defaultQueuePriority;
		queue_create_infos.push_back(queueInfo);

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo device_create_info = {
          VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,               // VkStructureType                  sType
          nullptr,                                            // const void                     * pNext
          0,                                                  // VkDeviceCreateFlags              flags
          static_cast<uint32_t>(queue_create_infos.size()),   // uint32_t                         queueCreateInfoCount
          queue_create_infos.data(),                          // const VkDeviceQueueCreateInfo  * pQueueCreateInfos
          0,                                                  // uint32_t                         enabledLayerCount
          nullptr,                                            // const char * const             * ppEnabledLayerNames
          static_cast<uint32_t>(desiredDeviceExtensions.size()),   // uint32_t                         enabledExtensionCount
          desiredDeviceExtensions.data(),                          // const char * const             * ppEnabledExtensionNames
          &deviceFeatures                                    // const VkPhysicalDeviceFeatures * pEnabledFeatures
        };


        VkResult result = vkCreateDevice(selectedPhysicalDevice, &device_create_info, nullptr, &s_vkDevice);

        if ((result != VK_SUCCESS) ||
            (s_vkDevice == VK_NULL_HANDLE)) {
            std::cout << "Could not create logical device." << std::endl;
        }

        s_vkPhysicalDevice = selectedPhysicalDevice;
}

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}


void RenderAPI_VulkanDX11::CreateVulkanImage(unsigned int width, unsigned int height, HANDLE* handle)
{
    // Get Image Format Properties supported by Physical Device
    VkPhysicalDeviceExternalImageFormatInfo externalImageFormatInfo = {};
    externalImageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    // Question: Should this be VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR or VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT ?
    externalImageFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

    VkPhysicalDeviceImageFormatInfo2 imageFormatInfo = {};
    imageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    imageFormatInfo.pNext = &externalImageFormatInfo;
    imageFormatInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageFormatInfo.type = VK_IMAGE_TYPE_2D;
    imageFormatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageFormatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageFormatInfo.flags = 0;

    VkExternalImageFormatProperties externalImageFormatProperties = {};
    externalImageFormatProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    
	VkImageFormatProperties2 imageFormatProperties = {};
    imageFormatProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    imageFormatProperties.pNext = &externalImageFormatProperties;

    if (vkGetPhysicalDeviceImageFormatProperties2(s_vkPhysicalDevice, &imageFormatInfo, &imageFormatProperties) != VK_SUCCESS) {
        // Handle the error (the requested format or external handle type is not supported)
        printf("Error here");
    } else {
        printf("Set debug brkpt here to examine what is returned in imageFormateProperties and externalImageFormatProperties");
    }


    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Enable external memory handle types
    VkExternalMemoryImageCreateInfo externalMemoryImageInfo = {};
    externalMemoryImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    // NB. The handle types includes VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
    externalMemoryImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR | VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    imageInfo.pNext = &externalMemoryImageInfo;

    VkImage image;
    vkCreateImage(s_vkDevice, &imageInfo, nullptr, &image);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(s_vkDevice, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_vkPhysicalDevice);

    // Enable export memory handle
    VkExportMemoryAllocateInfo exportAllocInfo = {};
    exportAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    // NB. The handle types includes VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
    exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR | VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    allocInfo.pNext = &exportAllocInfo;

    VkDeviceMemory imageMemory;
    vkAllocateMemory(s_vkDevice, &allocInfo, nullptr, &imageMemory);

    vkBindImageMemory(s_vkDevice, image, imageMemory, 0);


    VkMemoryGetWin32HandleInfoKHR getWin32HandleInfo = {};
    getWin32HandleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    getWin32HandleInfo.memory = imageMemory;
    // NB. The handle type (singular) takes VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
    getWin32HandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

    HANDLE externalHandle;
    if (vkGetMemoryWin32HandleKHR(s_vkDevice, &getWin32HandleInfo, &externalHandle) != VK_SUCCESS) {
        std::cout << "Error in obtaining memory handle: ";
    }

    *handle = externalHandle;

    // You can now pass this handle to other processes or Vulkan instances/devices
    // Someone has to call CloseHandle
    

    std::cout << "Memory handle exported as Win32 handle: " << handle << std::endl;
}


void RenderAPI_VulkanDX11::SetUnitySelectedDeviceId(int unitySelectedDeviceId)
{
    m_unitySelectedDeviceId = unitySelectedDeviceId;
}

RenderAPI_VulkanDX11* CreateRenderAPI_VulkanDX11()
{
    return new RenderAPI_VulkanDX11();
}

RenderAPI_VulkanDX11::RenderAPI_VulkanDX11()
    : 
	m_UnityVulkan(nullptr),
    m_Instance(),
	m_TextureStagingBuffer()
    , m_VertexStagingBuffer()
    , m_TrianglePipelineLayout(VK_NULL_HANDLE)
    , m_TrianglePipeline(VK_NULL_HANDLE)
    , m_TrianglePipelineRenderPass(VK_NULL_HANDLE)
{
}


void RenderAPI_VulkanDX11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    switch (type)
    {
    case kUnityGfxDeviceEventInitialize:
        
        // Make sure Vulkan API functions are loaded
        LoadVulkanAPI(vkGetInstanceProcAddr, s_vkInstance);

        // Create Device
        CreateVulkanDevice();


        //UnityVulkanPluginEventConfig config_1;
        //config_1.graphicsQueueAccess = kUnityVulkanGraphicsQueueAccess_DontCare;
        //config_1.renderPassPrecondition = kUnityVulkanRenderPass_EnsureInside;
        //config_1.flags = kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission | kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState;
        //m_UnityVulkan->ConfigureEvent(1, &config_1);

        // alternative way to intercept API
        //m_UnityVulkan->InterceptVulkanAPI("vkCmdBeginRenderPass", (PFN_vkVoidFunction)Hook_vkCmdBeginRenderPass);
        break;
    case kUnityGfxDeviceEventShutdown:

        if (m_Instance.device != VK_NULL_HANDLE)
        {
            // GarbageCollect();
        	if (m_TrianglePipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Instance.device, m_TrianglePipeline, NULL);
                m_TrianglePipeline = VK_NULL_HANDLE;
            }
            if (m_TrianglePipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(m_Instance.device, m_TrianglePipelineLayout, NULL);
                m_TrianglePipelineLayout = VK_NULL_HANDLE;
            }
        }

        m_UnityVulkan = NULL;
        m_TrianglePipelineRenderPass = VK_NULL_HANDLE;
        m_Instance = UnityVulkanInstance();

        break;
    }
}



#endif // #if SUPPORT_VULKAN
