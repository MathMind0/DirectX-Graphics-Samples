//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"
#include "Camera.h"
#include "StepTimer.h"
#include "SquidRoom.h"
#include "FrameResource.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

enum class CSU_DESCRIPTORS : UINT
{
    SHADOW_SRV = 2 + _countof(SampleAssets::Textures),
    SCREEN_COLOR_SRV,
    SCREEN_COLOR2_SRV,
    SCREEN_COLOR_UAV,
    SCREEN_COLOR2_UAV,
    NUM_DESCRIPTORS
};

enum class RTV_DESCRIPTORS : UINT
{
    SCREEN_COLOR_RTV = FrameCount,
    SCREEN_COLOR2_RTV,
    NUM_DESCRIPTORS
};

enum class DSV_DESCRIPTORS : UINT
{
    DEFAULT_DSV,
    SHADOW_DSV,
    NUM_DESCRIPTORS
};

enum class BLUR_METHOD : UINT
{
    BLUR_OFF,
    BLUR_NAIVE,
    BLUR_SEPARATE,
    BLUR_COMPUTE,
    BLUR_COMPUTE_COMBINED,
    BLUR_COMPUTE_BLOCK,
    NUM_BLUR_METHOD
};

enum class RENDER_SCENE_SIG_PARAMS : UINT
{
    DIFFUSE_NORMAL_TEX,
    SCENE_DATA_CB,
    SHADOW_MAP_TEX,
    SAMPLERS,
    NUM_PARAMS
};

enum class BLUR_SIG_PARAMS : UINT
{
    SCENE_COLOR_TEX,
    SCENE_INFO_CBV,
    NUM_PARAMS
};

enum class CSBLUR_SIG_PARAMS : UINT
{
    SCENE_COLOR_TEX,
    OUTPUT_UAV,
    NUM_PARAMS
};

class D3D12PostprocessBlur : public DXSample
{
public:
    D3D12PostprocessBlur(UINT width, UINT height, std::wstring name);
    virtual ~D3D12PostprocessBlur();

    static D3D12PostprocessBlur* Get() { return s_app; }

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);
    virtual void OnKeyUp(UINT8 key);

private:
    struct InputState
    {
        bool rightArrowPressed;
        bool leftArrowPressed;
        bool upArrowPressed;
        bool downArrowPressed;
        bool animate;
    };

    // Rendering Context
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_depthStencil;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvDepthStencil;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    D3D12_FEATURE_DATA_ROOT_SIGNATURE m_featureData;
    UINT m_compileFlags;

    // Descriptor Heaps
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;
    UINT m_defaultDescriptorSize;

    // Shadow Resources
    ComPtr<ID3D12Resource> m_shadowTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_shadowDepthView;
    D3D12_GPU_DESCRIPTOR_HANDLE m_shadowDepthHandle;
    ComPtr<ID3D12PipelineState> m_psoRenderShadow;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvNullGPU;
    bool    m_bUseD16 = true;
    
    // Scene Resources
    ComPtr<ID3D12RootSignature> m_sigRenderScene;
    ComPtr<ID3D12PipelineState> m_psoRenderScene;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    ComPtr<ID3D12Resource> m_textures[_countof(SampleAssets::Textures)];
    ComPtr<ID3D12Resource> m_textureUploads[_countof(SampleAssets::Textures)];
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_indexBufferUpload;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_vertexBufferUpload;    
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvFirstTextureGPU;

    // Postprocess Resources
    ComPtr<ID3D12Resource> m_texSceneColor;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvSceneColorCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvSceneColorGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvSceneColorCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavSceneColorGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavSceneColorCpu;
    ComPtr<ID3D12Resource> m_texSceneColor2;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvSceneColorCpu2;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvSceneColorGpu2;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvSceneColorCpu2;
    D3D12_GPU_DESCRIPTOR_HANDLE m_uavSceneColorGpu2;
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavSceneColorCpu2;
    ComPtr<ID3D12RootSignature> m_sigBlur;
    ComPtr<ID3D12PipelineState> m_psoBlur;
    ComPtr<ID3D12PipelineState> m_psoBlurX;
    ComPtr<ID3D12PipelineState> m_psoBlurY;
    ComPtr<ID3D12RootSignature> m_sigBlurCS;
    ComPtr<ID3D12PipelineState> m_psoBlurCSX;
    ComPtr<ID3D12PipelineState> m_psoBlurCSY;
    ComPtr<ID3D12PipelineState> m_psoBlurCSXCombined;
    ComPtr<ID3D12PipelineState> m_psoBlurCSYCombined;
    ComPtr<ID3D12PipelineState> m_psoBlurCSXBlock;
    ComPtr<ID3D12PipelineState> m_psoBlurCSYBlock;
    BLUR_METHOD m_blurMethod;

    // App data
    InputState m_keyboardInput;
    LightState m_lights[NumLights];
    Camera m_lightCameras[NumLights];
    Camera m_camera;
    StepTimer m_timer;
    StepTimer m_cpuTimer;
    int m_titleCount;
    double m_cpuTime;

    // Synchronization objects.
    UINT m_frameIndex;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent;    
    UINT64 m_fenceValue;

    // Singleton object so that worker threads can share members.
    static D3D12PostprocessBlur* s_app; 

    // Frame resources.
    FrameResource* m_frameResources[FrameCount];
    FrameResource* m_pCurrentFrameResource;
    int m_currentFrameResourceIndex;

    void SetCommonPipelineState(ID3D12GraphicsCommandList* pCommandList);

    void CreateRenderContext();
    void CreateSceneResources();
    void CreateDescriptorHeaps();
    void CreateSceneSignatures();
    void CreateScenePSOs();
    void CreateSamplers();
    void CreateDepthBuffer();
    void CreateSceneAssets();
    void LoadAssetVertexBuffer(const UINT8* pAssetData);
    void LoadAssetIndexBuffer(const UINT8* pAssetData);
    void LoadAssetTextures(const UINT8* pAssetData);
    void CreateLights();
    void CreateSyncFence();
    void CreateShadowResources();
    void CreatePostprocessResources();
    void CreateFrameResources();
    
    void RestoreD3DResources();
    void ReleaseD3DResources();
    void WaitForGpu();
    
    void BeginFrame();
    void RenderShadow();
    void RenderScene();
    void RenderBlurNaive();
    void RenderBlurSeparate();
    void RenderBlurCompute();
    void RenderBlurComputeCombined();
    void RenderBlurComputeBlock();
    void RenderPostprocess();
    void EndFrame();
    
};
