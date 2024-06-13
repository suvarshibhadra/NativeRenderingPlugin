using UnityEngine;
using System;
using System.Collections;
using System.Runtime.InteropServices;
using UnityEngine.Assertions;
using UnityEngine.Rendering;


public class UseRenderingPlugin : MonoBehaviour
{
    // Native plugin rendering events are only called if a plugin is used
    // by some script. This means we have to DllImport at least
    // one function in some active script.
    // For this example, we'll call into plugin's SetTimeFromUnity
    // function and pass the current time so the plugin can animate.

    [DllImport("RenderingPlugin")]
    private static extern void SetTimeFromUnity(float t);

    [DllImport("RenderingPlugin")]
    private static extern void SetMeshBuffersFromUnity(IntPtr vertexBuffer, int vertexCount, IntPtr sourceVertices, IntPtr sourceNormals, IntPtr sourceUVs);

    [DllImport("RenderingPlugin")]
    private static extern IntPtr GetRenderEventFunc();

    [DllImport("RenderingPlugin")]
    private static extern IntPtr CreateExternalVkImageForUnityTexture2D(int width, int height);

    private static RenderTexture renderTex;
    private static Texture2D texture2D;
    private static GameObject pluginInfo;

    IEnumerator Start() {
        if (SystemInfo.graphicsDeviceType == GraphicsDeviceType.Direct3D11) {
            CreateTexture2DWithVulkanCreatedImage();
        }

        SendMeshBuffersToPlugin();
        yield return StartCoroutine("CallPluginAtEndOfFrames");
    }

    // custom "time" for deterministic results
    int updateTimeCounter = 0;
    private IEnumerator CallPluginAtEndOfFrames() {
        while (true) {
            // Wait until all frame rendering is done
            yield return new WaitForEndOfFrame();

            ++updateTimeCounter;
            SetTimeFromUnity((float)updateTimeCounter * 0.016f);

            // Draw to Texture2D
            GL.IssuePluginEvent(GetRenderEventFunc(), 2);
        }
    }

    private void CreateTexture2DWithVulkanCreatedImage() {

        int width = 256;
        int height = 256;

        // Native call to retrieve handle of VkImage created by Vulkan and shared with DX11
        IntPtr externalTexturePtr = CreateExternalVkImageForUnityTexture2D(width, height);

        // Create a Texture 2D
        texture2D = Texture2D.CreateExternalTexture(width, height, TextureFormat.RGBA32, false, false, externalTexturePtr);
        IntPtr texture2DIntPtr = texture2D.GetNativeTexturePtr();

        // Verify the texture pointers are indeed the same
        Assert.IsTrue(texture2DIntPtr == externalTexturePtr);

        // Set texture onto our material
        GetComponent<Renderer>().material.mainTexture = texture2D;

        // Create a RenderTexture
        renderTex = new RenderTexture(256, 256, 16, RenderTextureFormat.ARGB32);
        renderTex.Create();

        // Show the RenderTexture on the sphere
        GameObject sphere = GameObject.Find("Sphere");
        sphere.transform.rotation = Quaternion.Euler(0.0f, 180.0f, 0.0f);
        sphere.GetComponent<Renderer>().material.mainTexture = renderTex;
    }

    void OnDisable()  {
        if (SystemInfo.graphicsDeviceType == GraphicsDeviceType.Direct3D11) {
            // Signals to the plugin that renderTex will be destroyed
        }
    }

    private void SendMeshBuffersToPlugin()  {
        var filter = GetComponent<MeshFilter>();
        var mesh = filter.mesh;

        // This is equivalent to MeshVertex in RenderingPlugin.cpp
        var desiredVertexLayout = new[]  {
            new VertexAttributeDescriptor(VertexAttribute.Position, VertexAttributeFormat.Float32, 3),
            new VertexAttributeDescriptor(VertexAttribute.Normal, VertexAttributeFormat.Float32, 3),
            new VertexAttributeDescriptor(VertexAttribute.Color, VertexAttributeFormat.Float32, 4),
            new VertexAttributeDescriptor(VertexAttribute.TexCoord0, VertexAttributeFormat.Float32, 2)
        };

        // Let's be certain we'll get the vertex buffer layout we want in native code
        mesh.SetVertexBufferParams(mesh.vertexCount, desiredVertexLayout);

        // The plugin will want to modify the vertex buffer -- on many platforms
        // for that to work we have to mark mesh as "dynamic" (which makes the buffers CPU writable --
        // by default they are immutable and only GPU-readable).
        mesh.MarkDynamic();

        // However, mesh being dynamic also means that the CPU on most platforms can not
        // read from the vertex buffer. Our plugin also wants original mesh data,
        // so let's pass it as pointers to regular C# arrays.
        // This bit shows how to pass array pointers to native plugins without doing an expensive
        // copy: you have to get a GCHandle, and get raw address of that.
        var vertices = mesh.vertices;
        var normals = mesh.normals;
        var uvs = mesh.uv;
        GCHandle gcVertices = GCHandle.Alloc(vertices, GCHandleType.Pinned);
        GCHandle gcNormals = GCHandle.Alloc(normals, GCHandleType.Pinned);
        GCHandle gcUV = GCHandle.Alloc(uvs, GCHandleType.Pinned);

        SetMeshBuffersFromUnity(mesh.GetNativeVertexBufferPtr(0), mesh.vertexCount, gcVertices.AddrOfPinnedObject(), gcNormals.AddrOfPinnedObject(), gcUV.AddrOfPinnedObject());

        gcVertices.Free();
        gcNormals.Free();
        gcUV.Free();
    }
}
