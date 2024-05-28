#pragma once

#include "Unity/IUnityGraphics.h"

#include <stddef.h>

#if defined (_WIN32)
//#include <vulkan/vulkan_win32.h>
#elif defined(__ANDROID__)
//#include <vulkan/vulkan_android.h>
#endif

struct IUnityInterfaces;

class VulkanHelperRenderAPI
{
public:
	virtual ~VulkanHelperRenderAPI() { }

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) = 0;

	// Is the API using "reversed" (1.0 at near plane, 0.0 at far plane) depth buffer?
	// Reversed Z is used on modern platforms, and improves depth buffer precision.
	virtual bool GetUsesReverseZ() = 0;
	virtual void LoadVulkanSharedLibrary();
	virtual void CreateVulkanInstance();
	virtual void LoadVulkanFnPtrs();
	virtual void CreateVulkanDevice();
	virtual void CreateVulkanImage();
};


// Create a graphics API implementation instance for the given API type.
VulkanHelperRenderAPI* CreateRenderAPI_VulkanDX11();

