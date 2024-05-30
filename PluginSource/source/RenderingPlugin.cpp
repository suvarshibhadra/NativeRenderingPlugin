// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"
#include "VulkanHelperRenderAPI.h"

#include <assert.h>
#include <d3d11.h>
#include <math.h>
#include <vector>

#include "Unity/IUnityGraphicsD3D11.h"


#if defined(_WIN32)
#include <windows.h>
#endif


static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

/* Unity Lifecycle
 * -Plugin
 * --Plugin Load (**)
 * --Setup
 * ---Frame
 * --Plugin Unload
 */
extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces * unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	/*
	// Not nessary since I'm not going to hook to anything
#if SUPPORT_VULKAN
	if (s_Graphics->GetRenderer() == kUnityGfxRendererNull)
	{
		// This stores the address of vkGetInstanceProcAddr
		// Calls UnityVulkanInterface->InterceptInitialization with hook fn ptr
		extern void RenderAPI_Vulkan_OnPluginLoad(IUnityInterfaces*);
		RenderAPI_Vulkan_OnPluginLoad(unityInterfaces);
	}
#endif // SUPPORT_VULKAN
*/

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// Plugin Unload
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}


// --------------------------------------------------------------------------
// GraphicsDeviceEvent

static ID3D11Device* s_d3d11Device = nullptr;
static RenderAPI* s_DX11_API = NULL;
static VulkanHelperRenderAPI* s_VulkanHelperAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;
extern RenderAPI* CreateRenderAPI_D3D11();

// This event is registered by UnityPluginLoad
// also called by UnityPluginLoad with
// the s_DeviceType (API) is obtained here by calling s_Graphics->GetRenderer();
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		assert(s_DX11_API == NULL);
		s_DeviceType = s_Graphics->GetRenderer();
		assert(s_DeviceType == kUnityGfxRendererD3D11);

		// Store the D3D11 Device
		if (IUnityGraphicsD3D11* d3d11Interface = s_UnityInterfaces->Get<IUnityGraphicsD3D11>()){
			s_d3d11Device = d3d11Interface->GetDevice();
		}
				
		s_DX11_API = CreateRenderAPI_D3D11();
		s_VulkanHelperAPI = CreateRenderAPI_VulkanDX11();

		{ // Load Library and Create Vulkan Instance
			s_VulkanHelperAPI->LoadVulkanSharedLibrary();
			s_VulkanHelperAPI->CreateVulkanInstance();
			// Vulkan Fn Pts and Device Creation in ProcessDeviceEvent(Init)
		}

	}

	// Let the implementation process the device related events
	if (s_DX11_API)	{
		s_DX11_API->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	if (s_VulkanHelperAPI)	{
		s_VulkanHelperAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown)
	{
		delete s_DX11_API;
		s_DX11_API = NULL;
		s_DeviceType = kUnityGfxRendererNull;

		delete s_VulkanHelperAPI;
		s_VulkanHelperAPI = NULL;
	}
}

// Some Fn Prototypes
static void DrawColoredTriangle();
static void ModifyTexturePixels();
static void drawToRenderTexture();
static void ModifyVertexBuffer();
static void drawToPluginTexture();

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_DX11_API == NULL)
		return;

	if (eventID == 1)
	{
		drawToRenderTexture();
		DrawColoredTriangle();
		ModifyTexturePixels();
		ModifyVertexBuffer();
	}

	if (eventID == 2)
	{
		drawToPluginTexture();
	}

}

// Return to Unity the Per-Frame Callback
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}

static float g_Time;
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity(float t) { g_Time = t; }

// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TextureHandle = NULL;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTextureFromUnity(void* textureHandle, int w, int h)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TextureHandle = textureHandle;
	g_TextureWidth = w;
	g_TextureHeight = h;
}

extern "C" __declspec(dllexport) void* CreateVkImageForUnityRenderTexture(void* renderTextureColorBufferHandle, int w, int h)
//extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CreateRenderTextureImageForUnity(int w, int h)
{

	#if defined(_WIN32)
	HANDLE nativeHandle;
	if(s_VulkanHelperAPI) {
		s_VulkanHelperAPI->CreateVulkanImage(w, h, &nativeHandle);

		ID3D11Texture2D* d3d11Texture = nullptr;
		HRESULT hr = s_d3d11Device->OpenSharedResource(nativeHandle, __uuidof(ID3D11Texture2D), (void**)&d3d11Texture);

		if (FAILED(hr)) {
			printf("Unable to open native handle from D3D11 device");
			// Handle error
			return 0;
		}

		// Need to pass the D3D11 Handle to Texture2.CreateExternalTexture

		// Then Graphics.Blit from Texture2D to RenderTexture in Unity.

	}
	return nativeHandle;
#endif

}


// --------------------------------------------------------------------------
// SetMeshBuffersFromUnity, an example function we export which is called by one of the scripts.

static void* g_VertexBufferHandle = NULL;
static int g_VertexBufferVertexCount;

struct MeshVertex
{
	float pos[3];
	float normal[3];
	float color[4];
	float uv[2];
};
static std::vector<MeshVertex> g_VertexSource;


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetMeshBuffersFromUnity(void* vertexBufferHandle, int vertexCount, float* sourceVertices, float* sourceNormals, float* sourceUV)
{
	// A script calls this at initialization time; just remember the pointer here.
	// Will update buffer data each frame from the plugin rendering event (buffer update
	// needs to happen on the rendering thread).
	g_VertexBufferHandle = vertexBufferHandle;
	g_VertexBufferVertexCount = vertexCount;

	// The script also passes original source mesh data. The reason is that the vertex buffer we'll be modifying
	// will be marked as "dynamic", and on many platforms this means we can only write into it, but not read its previous
	// contents. In this example we're not creating meshes from scratch, but are just altering original mesh data --
	// so remember it. The script just passes pointers to regular C# array contents.
	g_VertexSource.resize(vertexCount);
	for (int i = 0; i < vertexCount; ++i)
	{
		MeshVertex& v = g_VertexSource[i];
		v.pos[0] = sourceVertices[0];
		v.pos[1] = sourceVertices[1];
		v.pos[2] = sourceVertices[2];
		v.normal[0] = sourceNormals[0];
		v.normal[1] = sourceNormals[1];
		v.normal[2] = sourceNormals[2];
		v.uv[0] = sourceUV[0];
		v.uv[1] = sourceUV[1];
		sourceVertices += 3;
		sourceNormals += 3;
		sourceUV += 2;
	}
}

// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.


static void DrawColoredTriangle()
{
	// Draw a colored triangle. Note that colors will come out differently
	// in D3D and OpenGL, for example, since they expect color bytes
	// in different ordering.
	struct MyVertex
	{
		float x, y, z;
		unsigned int color;
	};
	MyVertex verts[3] =
	{
		{ -0.5f, -0.25f,  0, 0xFFff0000 },
		{ 0.5f, -0.25f,  0, 0xFF00ff00 },
		{ 0,     0.5f ,  0, 0xFF0000ff },
	};

	// Transformation matrix: rotate around Z axis based on time.
	float phi = g_Time; // time set externally from Unity script
	float cosPhi = cosf(phi);
	float sinPhi = sinf(phi);
	float depth = 0.7f;
	float finalDepth = s_DX11_API->GetUsesReverseZ() ? 1.0f - depth : depth;
	float worldMatrix[16] = {
		cosPhi,-sinPhi,0,0,
		sinPhi,cosPhi,0,0,
		0,0,1,0,
		0,0,finalDepth,1,
	};

	s_DX11_API->DrawSimpleTriangles(worldMatrix, 1, verts);
}


static void ModifyTexturePixels()
{
	void* textureHandle = g_TextureHandle;
	int width = g_TextureWidth;
	int height = g_TextureHeight;
	if (!textureHandle)
		return;

	int textureRowPitch;
	void* textureDataPtr = s_DX11_API->BeginModifyTexture(textureHandle, width, height, &textureRowPitch);
	if (!textureDataPtr)
		return;

	const float t = g_Time * 4.0f;

	unsigned char* dst = (unsigned char*)textureDataPtr;
	for (int y = 0; y < height; ++y)
	{
		unsigned char* ptr = dst;
		for (int x = 0; x < width; ++x)
		{
			// Simple "plasma effect": several combined sine waves
			int vv = int(
				(127.0f + (127.0f * sinf(x / 7.0f + t))) +
				(127.0f + (127.0f * sinf(y / 5.0f - t))) +
				(127.0f + (127.0f * sinf((x + y) / 6.0f - t))) +
				(127.0f + (127.0f * sinf(sqrtf(float(x*x + y*y)) / 4.0f - t)))
				) / 4;

			// Write the texture pixel
			ptr[0] = vv;
			ptr[1] = vv;
			ptr[2] = vv;
			ptr[3] = vv;

			// To next pixel (our pixels are 4 bpp)
			ptr += 4;
		}

		// To next image row
		dst += textureRowPitch;
	}

	s_DX11_API->EndModifyTexture(textureHandle, width, height, textureRowPitch, textureDataPtr);
}


static void ModifyVertexBuffer()
{
	void* bufferHandle = g_VertexBufferHandle;
	int vertexCount = g_VertexBufferVertexCount;
	if (!bufferHandle)
		return;

	size_t bufferSize;
	void* bufferDataPtr = s_DX11_API->BeginModifyVertexBuffer(bufferHandle, &bufferSize);
	if (!bufferDataPtr)
		return;
	int vertexStride = int(bufferSize / vertexCount);

	// Unity should return us a buffer that is the size of `vertexCount * sizeof(MeshVertex)`
	// If that's not the case then we should quit to avoid unexpected results.
	// This can happen if https://docs.unity3d.com/ScriptReference/Mesh.GetNativeVertexBufferPtr.html returns
	// a pointer to a buffer with an unexpected layout.
	if (static_cast<unsigned int>(vertexStride) != sizeof(MeshVertex))
		return;

	const float t = g_Time * 3.0f;

	char* bufferPtr = (char*)bufferDataPtr;
	// modify vertex Y position with several scrolling sine waves,
	// copy the rest of the source data unmodified
	for (int i = 0; i < vertexCount; ++i)
	{
		const MeshVertex& src = g_VertexSource[i];
		MeshVertex& dst = *(MeshVertex*)bufferPtr;
		dst.pos[0] = src.pos[0];
		dst.pos[1] = src.pos[1] + sinf(src.pos[0] * 1.1f + t) * 0.4f + sinf(src.pos[2] * 0.9f - t) * 0.3f;
		dst.pos[2] = src.pos[2];
		dst.normal[0] = src.normal[0];
		dst.normal[1] = src.normal[1];
		dst.normal[2] = src.normal[2];
		dst.uv[0] = src.uv[0];
		dst.uv[1] = src.uv[1];
		bufferPtr += vertexStride;
	}

	s_DX11_API->EndModifyVertexBuffer(bufferHandle);
}

static void drawToPluginTexture()
{
	s_DX11_API->drawToPluginTexture();
}

static void drawToRenderTexture()
{
	s_DX11_API->drawToRenderTexture();
}

