// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "VulkanExternalImageHandler.h"

#include <assert.h>
#include <d3d11_1.h>
#include <iostream>
#include <vector>

#include "Unity/IUnityGraphicsD3D11.h"

#if defined(_WIN32)
#include <windows.h>
#pragma comment(lib, "dxgi.lib")
#endif


static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

/* Unity Native Plugin Lifecycle
 * --Plugin Load
 * --- GraphicsDeviceEvent(Initialize)
 * --- OnRenderEvent (eventIds)
 * --- Unity C# -> Plugin Calls
 * --- GraphicsDeviceEvent(Initialize)
 * --Plugin Unload
 */
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// Plugin Unload
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

// GraphicsDeviceEvent

static ID3D11Device* s_d3d11Device = nullptr;
static VulkanExternalImageHandler* s_VulkanExternalImageHandler = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

// This event is registered by UnityPluginLoad
// also called by UnityPluginLoad with
// the s_DeviceType (API) is obtained here by calling s_Graphics->GetRenderer();
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		s_DeviceType = s_Graphics->GetRenderer();
		assert(s_DeviceType == kUnityGfxRendererD3D11);

		// Store the D3D11 Device
		if (IUnityGraphicsD3D11* d3d11Interface = s_UnityInterfaces->Get<IUnityGraphicsD3D11>()){
			s_d3d11Device = d3d11Interface->GetDevice();
		}

		s_VulkanExternalImageHandler = new VulkanExternalImageHandler(s_d3d11Device);

		{ // Load Library and Create Vulkan Instance
			VulkanExternalImageHandler::LoadVulkanSharedLibrary();
			s_VulkanExternalImageHandler->CreateVulkanInstance();
			// Vulkan Fn Pts and Device Creation in ProcessDeviceEvent(Init)
		}
	}

	if (s_VulkanExternalImageHandler)	{
		/* Load Vulkan Fn Ptrs that depend on VkInstance
		 * Select Physical Device
		 * Create Logical Device
		 */
		s_VulkanExternalImageHandler->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown)
	{
		s_DeviceType = kUnityGfxRendererNull;
		delete s_VulkanExternalImageHandler;
		s_VulkanExternalImageHandler = NULL;
	}
}

/*
 * OnRenderEvent - This will be called for GL.IssuePluginEvent script calls; eventID will
 * be the integer passed to IssuePluginEvent.
 */
static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_VulkanExternalImageHandler == NULL)
		return;

	// Use
	if (eventID == 1) {
		// Use Vulkan to draw to VkImage
	}
}

// Return to Unity the Per-Frame Callback
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc() { return OnRenderEvent; }

/*
 * Method called from Unity to obtain a shared handle that can be used to create a Texture2D via
 * https://docs.unity3d.com/ScriptReference/Texture2D.CreateExternalTexture.html
 * In DX11 the return value expected is a ID3D11Texture2D*
 */
extern "C" __declspec(dllexport) intptr_t CreateExternalVkImageForUnityTexture2D(int w, int h) {
#if defined(_WIN32)

	// Unity expects ID3D11Texture2D* d3d11Texture to pass to Texture2D.CreateExternalTexture
	// Docs: https://docs.unity3d.com/ScriptReference/Texture2D.CreateExternalTexture.html
	ID3D11Texture2D* d3d11Texture = nullptr;

	if(s_VulkanExternalImageHandler) {

		// Option:1 Vulkan creates External Image to share with DX11
		// Currently does not work since DX11 is not able to open shared image
		s_VulkanExternalImageHandler->DX11Handle_VulkanCreatedExternalImage(w, h, &d3d11Texture);
		if (d3d11Texture != nullptr) {
			return reinterpret_cast<intptr_t>(d3d11Texture);
		} 

		// Option2: DX11 creates External ID3D11Texture2D and shares with Vulkan
		// DX11 is able to create texture;  Unity is unable to create a texture with it:
		// https://issuetracker.unity3d.com/issues/warning-registering-a-native-texture-with-depth-equals-0-while-the-actual-texture-has-depth-equals-1-is-thrown-when-in-play-mode-and-creating-a-cubemap-from-another-cubemaps-native-texture-1
		ID3D11ShaderResourceView* shaderResourceView;
		s_VulkanExternalImageHandler->DX11Handle_VulkanShared_ExternalImage(w, h, &shaderResourceView);
		return reinterpret_cast<intptr_t>(shaderResourceView);
	}
#endif
	return reinterpret_cast<intptr_t>(nullptr);
}
