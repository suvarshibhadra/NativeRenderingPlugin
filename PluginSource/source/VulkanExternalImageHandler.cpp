#include <cassert>
#include <iostream>

#include "VulkanExternalImageHandler.h"

#include <cstring>
#include <map>
#include <vector>

// This plugin does not link to the Vulkan loader, easier to support multiple APIs and systems that don't have Vulkan support
#include <d3d11_1.h>

#include "Unity/IUnityGraphicsVulkan.h"

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#pragma comment(lib, "d3d11.lib")
#endif

// TODO Move to shared header
#include <algorithm>
#include <cassert>
#define VK_CHECK_RESULT(f) \
{ \
    VkResult res = (f); \
    if (res != VK_SUCCESS) \
    { \
        fprintf(stderr, "Fatal : VkResult is \" %d \" in %s at line %d\n", res, __FILE__, __LINE__); \
        assert(res == VK_SUCCESS); \
    } \
}

#define UNITY_USED_VULKAN_API_FUNCTIONS(apply) \
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

VulkanExternalImageHandler::VulkanExternalImageHandler(ID3D11Device* d3d11Device)
	: m_vkInstance(VK_NULL_HANDLE), m_vkPhysicalDevice(VK_NULL_HANDLE), m_vkDevice(VK_NULL_HANDLE), m_DebugUtilsMessenger(VK_NULL_HANDLE)
{
	m_d3d11Device = d3d11Device;

	IDXGIDevice* dxgiDevice;
	if (SUCCEEDED(d3d11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
	{
		IDXGIAdapter* dxgiAdapter;
		if (SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter)))
		{
			DXGI_ADAPTER_DESC desc;
			dxgiAdapter->GetDesc(&desc);
			// Store the Device Id to ensure the Vulkan Physical Device matches with it
			m_unitySelectedDeviceId = static_cast<int>(desc.DeviceId);
			dxgiAdapter->Release();
		}
		dxgiDevice->Release();
	}
}

//PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
void VulkanExternalImageHandler::LoadVulkanSharedLibrary()
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

VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        printf("{%d} - {%s}: %s{}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        printf("{%d} - {%s}: {%s}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
    }
    return VK_FALSE;
}

void VulkanExternalImageHandler::CreateVulkanInstance()
{
    static bool debugUtils = true;

	// We need these b/c Vulkan fn-ptrs are loaded after VkInstance creation
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
    
    // Specify Instance Extensions needed for external memory
    std::vector<const char*> desiredInstanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME
    };

    {  // Add optional instance extensions
#if defined(_WIN32)
        desiredInstanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif

        if (debugUtils) {
            desiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }
    
    {   // Ensure desired extensions are available
        std::vector<VkExtensionProperties> availableExtensions = {};
        {   // Populate available extensions
            uint32_t instanceExtensionPropertyCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionPropertyCount, nullptr);
            availableExtensions.resize(instanceExtensionPropertyCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionPropertyCount, availableExtensions.data());
        }

        // Iterate through Available extensions to ensure desired extensions are present
        for (const auto& extensionName : desiredInstanceExtensions) {

            bool existsInAvailableExtension = std::any_of(availableExtensions.begin(), availableExtensions.end(),
                [extensionName](const VkExtensionProperties& prop) {
                    return strcmp(prop.extensionName, extensionName) == 0;
                });

            if (!existsInAvailableExtension) {
                std::cout << "Desired extension does not exist in available extensions:" << extensionName << std::endl;
                return;
            }
        }
    }

    std::vector<const char*> desiredInstanceLayers = {};
    {   // Specify instance layers
        if (debugUtils) {
            const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
            desiredInstanceLayers.emplace_back(validationLayerName);
        }
    }

	{   // Ensure Desired Instance Layers are available
		uint32_t availableInstanceLayerCount = 0;
        std::vector<VkLayerProperties> availableInstanceLayers = {};
        vkEnumerateInstanceLayerProperties(&availableInstanceLayerCount, nullptr);
        availableInstanceLayers.resize(availableInstanceLayerCount);
        vkEnumerateInstanceLayerProperties(&availableInstanceLayerCount, availableInstanceLayers.data());

        for (const auto&instanceLayer : desiredInstanceLayers)
        {
            bool available = std::any_of(availableInstanceLayers.begin(), availableInstanceLayers.end(), [instanceLayer](const VkLayerProperties& layerProperties){
                return strcmp(instanceLayer, layerProperties.layerName) == 0;
                });

            if(!available){
                std::cout << "Did not find instance layer" << std::endl;
                return;
            }
        }
	}

    {   // Create Vulkan Instance with desired instance layers and extensions
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Unity Vulkan Plugin";
        appInfo.pEngineName = "";
        //TODO Maybe source this from vkEnumerateInstanceVersion 
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &appInfo;

        instanceCreateInfo.ppEnabledLayerNames = desiredInstanceLayers.data();
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(desiredInstanceLayers.size());

        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(desiredInstanceExtensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = desiredInstanceExtensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        if (debugUtils) {
            // Create Debug Messenger
            // From: https://docs.vulkan.org/samples/latest/samples/extensions/debug_utils/README.html

            debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
            debug_utils_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            debug_utils_create_info.pfnUserCallback = debug_utils_messenger_callback;

            instanceCreateInfo.pNext = &debug_utils_create_info;
        }

        VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &m_vkInstance))

            if (debugUtils) {
                PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_vkInstance, "vkCreateDebugUtilsMessengerEXT");
                VK_CHECK_RESULT(vkCreateDebugUtilsMessengerEXT(m_vkInstance, &debug_utils_create_info, nullptr, &m_DebugUtilsMessenger))
            }
    }

}

void VulkanExternalImageHandler::LoadVulkanFnPtrs()
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

void VulkanExternalImageHandler::CreateVulkanDevice()
{
    VkPhysicalDevice selectedPhysicalDevice = {};
    int gfxQueueFamilyIndexOfSelectedDevice = -1;

    std::vector<VkPhysicalDevice> available_devices = {};
    EnumerateAvailablePhysicalDevices(m_vkInstance, available_devices);

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


        VkResult result = vkCreateDevice(selectedPhysicalDevice, &device_create_info, nullptr, &m_vkDevice);

        if ((result != VK_SUCCESS) ||
            (m_vkDevice == VK_NULL_HANDLE)) {
            std::cout << "Could not create logical device." << std::endl;
        }

        m_vkPhysicalDevice = selectedPhysicalDevice;
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


void VulkanExternalImageHandler::DX11Handle_VulkanCreatedExternalImage(unsigned int width, unsigned int height, ID3D11Texture2D** texture2DHandle)
{
       // Check if Physical Device Supports the External Image Format Needed

        VkExternalImageFormatProperties externalImageFormatProperties = {};
        VkImageFormatProperties2 imageFormatProperties = {};

		// Obtain the external ImageFormat properties

            // Get Image Format Properties supported by Physical Device
            VkPhysicalDeviceExternalImageFormatInfo physicalDeviceExternalImageFormatInfo = {};
            physicalDeviceExternalImageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
            /* Why VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR and not VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT ?
             * Per https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkExternalMemoryHandleTypeFlagBits.html
             * VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT specifies an NT handle returned by IDXGIResource1::CreateSharedHandle
             * referring to a Direct3D 10 or 11 texture resource. It owns a reference to the memory used by the Direct3D resource.
             */
            physicalDeviceExternalImageFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

            VkPhysicalDeviceImageFormatInfo2 physicalDeviceImageFormatInfo = {};
            physicalDeviceImageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            physicalDeviceImageFormatInfo.pNext = &physicalDeviceExternalImageFormatInfo;
            physicalDeviceImageFormatInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            physicalDeviceImageFormatInfo.type = VK_IMAGE_TYPE_2D;
            physicalDeviceImageFormatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            physicalDeviceImageFormatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            physicalDeviceImageFormatInfo.flags = 0;

            
            externalImageFormatProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

            
            imageFormatProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
            imageFormatProperties.pNext = &externalImageFormatProperties;

            VK_CHECK_RESULT(vkGetPhysicalDeviceImageFormatProperties2(m_vkPhysicalDevice, &physicalDeviceImageFormatInfo, &imageFormatProperties))
    

        /* Check the external image format meets our needs
         * Compatible Handle Types includes VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
         * Docs: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkExternalMemoryHandleTypeFlagBits.html
         *
         * External Memory Features includes VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT
         * Docs: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkExternalMemoryFeatureFlagBits.html
         */ 
    	if (!((externalImageFormatProperties.externalMemoryProperties.compatibleHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR) &&
            (externalImageFormatProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)) &&
			(externalImageFormatProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT_KHR) == 0) {

            std::cout << "Request format not compatible " << std::endl;
            return;
        }
    
        
    VkImage vkImage = VK_NULL_HANDLE;
    {   // Create VkImage

		/* Docs:
		 * 1. https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkImageCreateInfo.html#VUID-VkImageCreateInfo-pNext-00990
		 * 2. https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkImageCreateInfo.html
		 *
		 * If the pNext chain includes a VkExternalMemoryImageCreateInfo structure, its handleTypes member must only contain bits that are also in
         * VkExternalImageFormatProperties::externalMemoryProperties.compatibleHandleTypes, as returned by
         * vkGetPhysicalDeviceImageFormatProperties2 with format, imageType, tiling, usage, and flags equal to those in this structure,
         * and with a VkPhysicalDeviceExternalImageFormatInfo structure included in the pNext chain, with a handleType equal
         * to any one of the handle types specified in VkExternalMemoryImageCreateInfo::handleTypes
		 */ 
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.format = physicalDeviceImageFormatInfo.format;
        imageInfo.imageType = physicalDeviceImageFormatInfo.type;
        imageInfo.tiling = physicalDeviceImageFormatInfo.tiling;
        imageInfo.usage = physicalDeviceImageFormatInfo.usage;
        imageInfo.flags = physicalDeviceImageFormatInfo.flags;
		imageInfo.extent = { width, height, 0 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // Enable external memory handle types in vkCreateImageInfo
        // Docs: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkExternalMemoryImageCreateInfo.html
        VkExternalMemoryImageCreateInfoKHR externalMemoryImageInfo = {};
        externalMemoryImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        // NB. The handle types includes VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT but not VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
        externalMemoryImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
        imageInfo.pNext = &externalMemoryImageInfo;

        VK_CHECK_RESULT(vkCreateImage(m_vkDevice, &imageInfo, nullptr, &vkImage))
    }

    VkDeviceMemory imageMemory;
    {   // Allocate and bind Memory to VK Image

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_vkDevice, vkImage, &memRequirements);

        /* Docs:
         * 1. https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkMemoryAllocateInfo.html
         *  If the pNext chain includes a VkExportMemoryAllocateInfo structure, and any of the handle types specified in VkExportMemoryAllocateInfo::handleTypes
         *  require a dedicated allocation,
         *  as reported by vkGetPhysicalDeviceImageFormatProperties2 in VkExternalImageFormatProperties::externalMemoryProperties.externalMemoryFeatures,
         *  the pNext chain must include a VkMemoryDedicatedAllocateInfo
         *  with its image member set to a value other than VK_NULL_HANDLE
         */
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vkPhysicalDevice);

        /* Docs:
         * 1. https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkExportMemoryAllocateInfo.html
         */ 
        VkExportMemoryAllocateInfo exportAllocInfo = {};
        exportAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        // NB. The handle types does not include VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT; is this correct?
        exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR ;
        allocInfo.pNext = &exportAllocInfo;

        VK_CHECK_RESULT(vkAllocateMemory(m_vkDevice, &allocInfo, nullptr, &imageMemory))
        VK_CHECK_RESULT(vkBindImageMemory(m_vkDevice, vkImage, imageMemory, 0))
    }

    HANDLE externalHandle = nullptr;
    {
		/* Docs: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkMemoryGetWin32HandleInfoKHR.html
		 * 
		 */
        VkMemoryGetWin32HandleInfoKHR getWin32HandleInfo = {};
        getWin32HandleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        getWin32HandleInfo.memory = imageMemory;
        // NB. The handle type (singular) takes VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
        getWin32HandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

        VK_CHECK_RESULT(vkGetMemoryWin32HandleKHR(m_vkDevice, &getWin32HandleInfo, &externalHandle))
    }
    

    /*
     *  Obtain DX11 Handle
     */

    /* Docs:
     * Using DX11_1 extension API https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-opensharedresource1
     * as docs give explicit example of opening ID3D11Texture2D* from HANDLE.
     * Also https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
     */
    ID3D11Texture2D* texture2D;
    HRESULT hr = m_d3d11Device->OpenSharedResource(externalHandle, __uuidof(ID3D11Texture2D), (void**)(&texture2D));
    // Exception thrown at 0x00007FFE5163BA99 in Unity.exe: Microsoft C++ exception: _com_error at memory location 0x000000B0EA9DD610.
	if (FAILED(hr)) {
        printf("Unable to open native handle from D3D11 device");
    }
    else {
        *texture2DHandle = texture2D;
    }
    
}

void VulkanExternalImageHandler::DX11Handle_VulkanShared_ExternalImage(unsigned int width, unsigned int height, ID3D11ShaderResourceView** outShaderResourceView)
{
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
	HANDLE handle;
    
    /* Reference Example: https://developer.nvidia.com/getting-vulkan-ready-vr
     */
	D3D11_TEXTURE2D_DESC descColor = {};
    descColor.Width = width;
    descColor.Height = height;
    // Set to -1 to indicate all the mipmap levels from MostDetailedMip on down to least detailed. 1 mean 1 level only.
    descColor.MipLevels = 1;
    descColor.ArraySize = 1;
    descColor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descColor.SampleDesc.Count = 1;
    descColor.Usage = D3D11_USAGE_DEFAULT;
    descColor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    descColor.CPUAccessFlags = 0;
	//descColor.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	
    if (SUCCEEDED(m_d3d11Device->CreateTexture2D(&descColor, nullptr, &texture))){
        IDXGIResource1* DxgiResource1;
        
        // Create a handle for sharing with Vulkan
        if (SUCCEEDED(texture->QueryInterface(&DxgiResource1))) {
            if (SUCCEEDED(DxgiResource1->CreateSharedHandle(
                NULL, GENERIC_ALL,
                nullptr,
                &handle))) {
                printf("Successfully created shared handle");
            }
        }

        // Create a Shader Resource View
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = descColor.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (SUCCEEDED(m_d3d11Device->CreateShaderResourceView(texture, &srvDesc, &shaderResourceView))) {
            printf("Successfully created shared handle");
            *outShaderResourceView = shaderResourceView;
        }

    } else {
        printf("Unable to create shared texture in DX11");
    }
}

void VulkanExternalImageHandler::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    switch (type)
    {
    case kUnityGfxDeviceEventInitialize:
        
        // Make sure Vulkan API functions are loaded
        LoadVulkanAPI(vkGetInstanceProcAddr, m_vkInstance);

        // Create Device
        CreateVulkanDevice();

        break;
    case kUnityGfxDeviceEventShutdown:

        // vkDestroy all Vulkan objects created here
        // set ivars to NULL and VK_NULL_HANDLE

        break;
case kUnityGfxDeviceEventBeforeReset:
        break;
case kUnityGfxDeviceEventAfterReset:
	break;
default: ;
    }
}


