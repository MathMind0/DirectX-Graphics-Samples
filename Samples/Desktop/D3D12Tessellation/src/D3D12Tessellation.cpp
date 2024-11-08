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

#include "stdafx.h"
#include "D3D12Tessellation.h"
#include "FrameResource.h"

D3D12Tessellation* D3D12Tessellation::s_app = nullptr;

D3D12Tessellation::D3D12Tessellation(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_keyboardInput(),
    m_titleCount(0),
    m_cpuTime(0),
    m_fenceValue(0),
    m_rtvDescriptorSize(0),
    m_currentFrameResourceIndex(0),
    m_pCurrentFrameResource(nullptr)
{
    s_app = this;

    m_keyboardInput.animate = true;

    ThrowIfFailed(DXGIDeclareAdapterRemovalSupport());
}

D3D12Tessellation::~D3D12Tessellation()
{
    s_app = nullptr;
}

void D3D12Tessellation::OnInit()
{
    CreateRenderContext();
    CreateSceneResources();
}

// Load the rendering pipeline dependencies.
void D3D12Tessellation::CreateRenderContext()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    NAME_D3D12_OBJECT(m_commandQueue);

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // This is the highest version the sample supports.
    // If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    m_featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
        &m_featureData, sizeof(m_featureData))))
    {
        m_featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
}

void D3D12Tessellation::CreateDescriptorHeaps()
{
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = (UINT)RTV_DESCRIPTORS::NUM_DESCRIPTORS; 
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    // Describe and create a depth stencil view (DSV) descriptor heap.
    // Each frame has its own depth stencils (to write shadows onto) 
    // and then there is one for the scene itself.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = (UINT)DSV_DESCRIPTORS::NUM_DESCRIPTORS; 
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

#if 0
    // Describe and create a shader resource view (SRV) and constant 
    // buffer view (CBV) descriptor heap.  
    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = (UINT)CSU_DESCRIPTORS::NUM_DESCRIPTORS;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));
    NAME_D3D12_OBJECT(m_cbvSrvHeap);


    // Describe and create a sampler descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 2;        // One clamp and one wrap sampler.
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)));
    NAME_D3D12_OBJECT(m_samplerHeap);
#endif
    
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    //m_defaultDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3D12Tessellation::CreateFrameResources()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    ComPtr<ID3D12Resource> backBuffer;
    
    for (UINT i = 0; i < FrameCount; i++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        m_frameResources[i] = new FrameResource(m_device.Get(),
            backBuffer.Get(), rtvHandle, i);
        
        m_frameResources[i]->WriteConstantBuffers(m_viewport, &m_camera,
            m_lightCameras, m_lights, NumLights,
            (float)m_timer.GetTotalSeconds(), INSTANCE_SCALE);
        
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        NAME_D3D12_OBJECT(backBuffer);
    }
}

void D3D12Tessellation::CreateSceneSignatures()
{
    //CD3DX12_DESCRIPTOR_RANGE1 ranges[8];
    CD3DX12_ROOT_PARAMETER1 rootParameters[8];

    rootParameters[(UINT)RENDER_SCENE_SIG_PARAMS::SCENE_DATA_CB].InitAsConstantBufferView(
        0, 0,
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1((UINT)RENDER_SCENE_SIG_PARAMS::NUM_PARAMS, rootParameters,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, m_featureData.HighestVersion,
        &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_sigRenderScene)));
    NAME_D3D12_OBJECT(m_sigRenderScene);
}

void D3D12Tessellation::CreateScenePSOs()
{
    struct
    {
        byte* code;
        UINT size;
    } vs, hs, ds, ps;
    
    ReadDataFromFile(GetAssetFullPath(L"ShadingVS.cso").c_str(),
        &vs.code, &vs.size);
    ReadDataFromFile(GetAssetFullPath(L"ShadingHS.cso").c_str(),
        &hs.code, &hs.size);
    ReadDataFromFile(GetAssetFullPath(L"ShadingDS.cso").c_str(),
        &ds.code, &ds.size);    
    ReadDataFromFile(GetAssetFullPath(L"ShadingPS.cso").c_str(),
        &ps.code, &ps.size);

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "MATWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
            1, 0,
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "MATWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
            1, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "MATWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,
            1, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
    };

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencilDesc.StencilEnable = FALSE;

    // Describe and create the PSO for rendering the scene.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = m_sigRenderScene.Get();
    psoDesc.VS = {vs.code, vs.size};
    psoDesc.HS = {hs.code, hs.size};
    psoDesc.DS = {ds.code, ds.size};
    psoDesc.PS = {ps.code, ps.size};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoRenderScene)));
    NAME_D3D12_OBJECT(m_psoRenderScene);
}

void D3D12Tessellation::CreateDepthBuffer()
{
    CD3DX12_RESOURCE_DESC depthBufferDesc(
            D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            0,
            static_cast<UINT>(m_viewport.Width), 
            static_cast<UINT>(m_viewport.Height), 
            1,
            1,
            DXGI_FORMAT_D16_UNORM,
            1, 
            0,
            D3D12_TEXTURE_LAYOUT_UNKNOWN,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

    D3D12_CLEAR_VALUE clearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = DXGI_FORMAT_D16_UNORM;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthBufferDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthStencil)));

    NAME_D3D12_OBJECT(m_depthStencil);

    m_dsvDepthStencil = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    // Create the depth stencil view.
    m_device->CreateDepthStencilView(m_depthStencil.Get(),
        nullptr, m_dsvDepthStencil);
}

void D3D12Tessellation::CreateSceneAssets()
{
    // Define the geometry for a patch.
    Vertex patchVertices[] =
    {
        { { -1.0f, 0.f, -1.0f } },
        { { 1.0f, 0.f, -1.0f } },
        { { -1.0f, 0.f, 1.0f } },
        { { 1.0f, 0.f, 1.0f } }
    };

    const UINT vertexBufferSize = sizeof(patchVertices);

    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)));

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, patchVertices, sizeof(patchVertices));
    m_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    Instance patchInstances[INSTANCE_NUM];
    XMMATRIX matScaling = XMMatrixScaling(INSTANCE_SCALE, INSTANCE_SCALE, INSTANCE_SCALE);
    for (UINT i = 0; i < INSTANCE_NUM; ++i)
    {
        XMMATRIX matTranslation = XMMatrixTranslation(0.f, 0.f, -INSTANCE_DISTANCE * i);
        XMMATRIX matPatch = matScaling * matTranslation;
        XMStoreFloat3x4(&patchInstances[i].matWorld, matPatch); 
    }
    
    const UINT instanceBufferSize = sizeof(patchInstances);

    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_instanceBuffer)));

    // Copy the triangle data to the vertex buffer.
    UINT8* pInstanceDataBegin;
    ThrowIfFailed(m_instanceBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pInstanceDataBegin)));
    memcpy(pInstanceDataBegin, patchInstances, sizeof(patchInstances));
    m_instanceBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_instanceBufferView.BufferLocation = m_instanceBuffer->GetGPUVirtualAddress();
    m_instanceBufferView.StrideInBytes = sizeof(Instance);
    m_instanceBufferView.SizeInBytes = instanceBufferSize;
}

#if 0
void D3D12Tessellation::CreateSamplers()
{
    // Get the sampler descriptor size for the current device.
    const UINT samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // Get a handle to the start of the descriptor heap.
    CD3DX12_CPU_DESCRIPTOR_HANDLE samplerHandle(m_samplerHeap->GetCPUDescriptorHandleForHeapStart());

    // Describe and create the wrapping sampler, which is used for 
    // sampling diffuse/normal maps.
    D3D12_SAMPLER_DESC wrapSamplerDesc = {};
    wrapSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    wrapSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    wrapSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    wrapSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    wrapSamplerDesc.MinLOD = 0;
    wrapSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    wrapSamplerDesc.MipLODBias = 0.0f;
    wrapSamplerDesc.MaxAnisotropy = 1;
    wrapSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    wrapSamplerDesc.BorderColor[0] = wrapSamplerDesc.BorderColor[1] =
        wrapSamplerDesc.BorderColor[2] = wrapSamplerDesc.BorderColor[3] = 0;
    m_device->CreateSampler(&wrapSamplerDesc, samplerHandle);

    // Move the handle to the next slot in the descriptor heap.
    samplerHandle.Offset(samplerDescriptorSize);

    // Describe and create the point clamping sampler, which is 
    // used for the shadow map.
    D3D12_SAMPLER_DESC clampSamplerDesc = {};
    clampSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    clampSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    clampSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    clampSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    clampSamplerDesc.MipLODBias = 0.0f;
    clampSamplerDesc.MaxAnisotropy = 1;
    clampSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    clampSamplerDesc.BorderColor[0] = clampSamplerDesc.BorderColor[1] =
        clampSamplerDesc.BorderColor[2] = clampSamplerDesc.BorderColor[3] = 0;
    clampSamplerDesc.MinLOD = 0;
    clampSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    m_device->CreateSampler(&clampSamplerDesc, samplerHandle);
}
#endif

void D3D12Tessellation::CreateLights()
{
    // Create lights.
    for (int i = 0; i < NumLights; i++)
    {
        // Set up each of the light positions and directions (they all start 
        // in the same place).
        m_lights[i].position = { 0.0f, 15.0f, -30.0f, 1.0f };
        m_lights[i].direction = { 0.0, 0.0f, 1.0f, 0.0f };
        m_lights[i].falloff = { 800.0f, 1.0f, 0.0f, 1.0f };
        m_lights[i].color = { 0.7f, 0.7f, 0.7f, 1.0f };

        XMVECTOR eye = XMLoadFloat4(&m_lights[i].position);
        XMVECTOR lookTo = XMLoadFloat4(&m_lights[i].direction);
        m_lightCameras[i].Set(eye, lookTo);
    }    
}

void D3D12Tessellation::CreateSyncFence()
{
    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    ThrowIfFailed(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue++;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.

    // Signal and increment the fence value.
    const UINT64 fenceToWaitFor = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceToWaitFor));
    m_fenceValue++;

    // Wait until the fence is completed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWaitFor, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

// Load the sample assets.
void D3D12Tessellation::CreateSceneResources()
{
    CreateDescriptorHeaps();
    CreateDepthBuffer();
    CreateSceneSignatures();
    CreateScenePSOs();
    //CreateSamplers();
    CreateSceneAssets();    
    CreateLights();

    // Close the command list and use it to execute the initial GPU setup.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    
    CreateFrameResources();
    
    m_currentFrameResourceIndex = 0;
    m_pCurrentFrameResource = m_frameResources[m_currentFrameResourceIndex];

    CreateSyncFence();
}

// Update frame-based values.
void D3D12Tessellation::OnUpdate()
{
    m_timer.Tick(NULL);

    PIXSetMarker(m_commandQueue.Get(), 0, L"Getting last completed fence.");

    // Get current GPU progress against submitted workload. Resources still scheduled 
    // for GPU execution cannot be modified or else undefined behavior will result.
    const UINT64 lastCompletedFence = m_fence->GetCompletedValue();

    // Make sure that this frame resource isn't still in use by the GPU.
    // If it is, wait for it to complete.
    if (m_pCurrentFrameResource->fenceValue > lastCompletedFence)
    {
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (eventHandle == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_pCurrentFrameResource->fenceValue, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    m_cpuTimer.Tick(NULL);
    float frameTime = static_cast<float>(m_timer.GetElapsedSeconds());
    float frameChange = 2.0f * frameTime;
    float moveDistance = MOVE_SPEED * frameTime;

    if (m_keyboardInput.leftArrowPressed)
        m_camera.RotateYaw(frameChange);
    if (m_keyboardInput.rightArrowPressed)
        m_camera.RotateYaw(-frameChange);
    if (m_keyboardInput.upArrowPressed)
        m_camera.RotatePitch(frameChange);
    if (m_keyboardInput.downArrowPressed)
        m_camera.RotatePitch(-frameChange);
    if (m_keyboardInput.MoveForwardPressed)
        m_camera.MoveForward(moveDistance);
    if (m_keyboardInput.MoveBackwardPressed)
        m_camera.MoveForward(-moveDistance);
    if (m_keyboardInput.StrafeRightPressed)
        m_camera.Strafe(moveDistance);
    if (m_keyboardInput.StrafeLeftPressed)
        m_camera.Strafe(-moveDistance);
    if (m_keyboardInput.ElevateUpPressed)
        m_camera.Elevate(moveDistance);
    if (m_keyboardInput.ElevateDownPressed)
        m_camera.Elevate(-moveDistance);

    if (m_keyboardInput.animate)
    {
        for (int i = 0; i < NumLights; i++)
        {
            float direction = frameChange * powf(-1.0f, static_cast<float>(i));
            XMStoreFloat4(&m_lights[i].position,
                XMVector4Transform(XMLoadFloat4(&m_lights[i].position), XMMatrixRotationY(direction)));

            XMVECTOR eye = XMLoadFloat4(&m_lights[i].position);
            XMVECTOR at = XMVectorSet(0.0f, 8.0f, 0.0f, 0.0f);
            XMVECTOR lookTo = XMVector3Normalize(XMVectorSubtract(at, eye));
            XMStoreFloat4(&m_lights[i].direction, lookTo);            
            m_lightCameras[i].Set(eye, lookTo);

            m_lightCameras[i].Get3DViewProjMatrices(&m_lights[i].view, &m_lights[i].projection,
                90.0f, static_cast<float>(m_width), static_cast<float>(m_height));
        }
    }

    m_pCurrentFrameResource->WriteConstantBuffers(m_viewport, &m_camera, m_lightCameras, m_lights, NumLights,
        (float)m_timer.GetTotalSeconds(), INSTANCE_SCALE);
}

// Render the scene.
void D3D12Tessellation::OnRender()
{
    try
    {
        ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
                
        BeginFrame();
        RenderScene();
        EndFrame();
        
        m_cpuTimer.Tick(NULL);
        if (m_titleCount == TitleThrottle)
        {
            WCHAR cpu[64];
            swprintf_s(cpu, L"%.4f CPU", m_cpuTime / m_titleCount);
            SetCustomWindowText(cpu);

            m_titleCount = 0;
            m_cpuTime = 0;
        }
        else
        {
            m_titleCount++;
            m_cpuTime += m_cpuTimer.GetElapsedSeconds() * 1000;
            m_cpuTimer.ResetElapsedTime();
        }

        // Present and update the frame index for the next frame.
        PIXBeginEvent(m_commandQueue.Get(), 0, L"Presenting to screen");
        ThrowIfFailed(m_swapChain->Present(1, 0));
        PIXEndEvent(m_commandQueue.Get());

        // Signal and increment the fence value.
        m_pCurrentFrameResource->fenceValue = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue));
        m_fenceValue++;
        
        // Move to the next frame resource.
        m_frameIndex ++;
        m_currentFrameResourceIndex = m_swapChain->GetCurrentBackBufferIndex();
        m_pCurrentFrameResource = m_frameResources[m_currentFrameResourceIndex];
    }
    catch (HrException& e)
    {
        if (e.Error() == DXGI_ERROR_DEVICE_REMOVED || e.Error() == DXGI_ERROR_DEVICE_RESET)
        {
            RestoreD3DResources();
        }
        else
        {
            throw;
        }
    }
}

void D3D12Tessellation::BeginFrame()
{    
    // Reset the command allocator and list.
    ThrowIfFailed(m_pCurrentFrameResource->commandAllocator->Reset());
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    ThrowIfFailed(commandList->Reset(m_pCurrentFrameResource->commandAllocator.Get(), nullptr));

    PIXBeginEvent(commandList, 0, L"Rendering a Frame");
    
    //ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get(), m_samplerHeap.Get() };
    //commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
}

void D3D12Tessellation::EndFrame()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    PIXEndEvent(commandList);
    ThrowIfFailed(commandList->Close());

    ID3D12CommandList* commandLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
}

void D3D12Tessellation::RenderScene()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    
    PIXBeginEvent(commandList, 0, L"Rendering Scene");
    
    // Scene pass. We use constant buf #2 and depth stencil #2
    // with rendering to the render target enabled.
    
    // Transition the shadow map from writeable to readable.
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrentFrameResource->backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };

    commandList->ResourceBarrier(_countof(barriers), barriers);

    // Clear the render target and depth stencil.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(m_pCurrentFrameResource->rtvBackBuffer, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(m_dsvDepthStencil, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(1,
        &m_pCurrentFrameResource->rtvBackBuffer,
        FALSE, &m_dsvDepthStencil);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);
    
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetVertexBuffers(1, 1, &m_instanceBufferView);

    commandList->SetPipelineState(m_psoRenderScene.Get());
    commandList->SetGraphicsRootSignature(m_sigRenderScene.Get());
    commandList->SetGraphicsRootConstantBufferView(
        (UINT)RENDER_SCENE_SIG_PARAMS::SCENE_DATA_CB,
        m_pCurrentFrameResource->cbScene->GetGPUVirtualAddress());
    
    commandList->DrawInstanced(4, INSTANCE_NUM, 0, 0);
    
    PIXEndEvent(commandList);
}

// Release sample's D3D objects.
void D3D12Tessellation::ReleaseD3DResources()
{
    m_fence.Reset();
    m_commandQueue.Reset();
    m_swapChain.Reset();
    m_device.Reset();
}

// Tears down D3D resources and reinitializes them.
void D3D12Tessellation::RestoreD3DResources()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        WaitForGpu();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    ReleaseD3DResources();
    OnInit();
}

// Wait for pending GPU work to complete.
void D3D12Tessellation::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
}

void D3D12Tessellation::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    {
        const UINT64 fence = m_fenceValue;
        const UINT64 lastCompletedFence = m_fence->GetCompletedValue();

        // Signal and increment the fence value.
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue));
        m_fenceValue++;

        // Wait until the previous frame is finished.
        if (lastCompletedFence < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        
        CloseHandle(m_fenceEvent);
    }

    for (int i = 0; i < _countof(m_frameResources); i++)
    {
        delete m_frameResources[i];
    }

    ReleaseD3DResources();
}

void D3D12Tessellation::OnKeyDown(UINT8 key)
{
    switch (key)
    {
    case VK_LEFT:
        m_keyboardInput.leftArrowPressed = true;
        break;
    case VK_RIGHT:
        m_keyboardInput.rightArrowPressed = true;
        break;
    case VK_UP:
        m_keyboardInput.upArrowPressed = true;
        break;
    case VK_DOWN:
        m_keyboardInput.downArrowPressed = true;
        break;
    case 'W':
    case 'w':
        m_keyboardInput.MoveForwardPressed = true;
        break;
    case 'S':
    case 's':
        m_keyboardInput.MoveBackwardPressed = true;
        break;
    case 'D':
    case 'd':
        m_keyboardInput.StrafeRightPressed = true;
        break;
    case 'A':
    case 'a':
        m_keyboardInput.StrafeLeftPressed = true;
        break;
    case 'E':
    case 'e':
        m_keyboardInput.ElevateUpPressed = true;
        break;
    case 'C':
    case 'c':
        m_keyboardInput.ElevateDownPressed = true;
        break;         
    case VK_SPACE:
        m_keyboardInput.animate = !m_keyboardInput.animate;
        break;
    }
}

void D3D12Tessellation::OnKeyUp(UINT8 key)
{
    switch (key)
    {
    case VK_LEFT:
        m_keyboardInput.leftArrowPressed = false;
        break;
    case VK_RIGHT:
        m_keyboardInput.rightArrowPressed = false;
        break;
    case VK_UP:
        m_keyboardInput.upArrowPressed = false;
        break;
    case VK_DOWN:
        m_keyboardInput.downArrowPressed = false;
        break;
    case 'W':
    case 'w':
        m_keyboardInput.MoveForwardPressed = false;
        break;
    case 'S':
    case 's':
        m_keyboardInput.MoveBackwardPressed = false;
        break;
    case 'D':
    case 'd':
        m_keyboardInput.StrafeRightPressed = false;
        break;
    case 'A':
    case 'a':
        m_keyboardInput.StrafeLeftPressed = false;
        break;
    case 'E':
    case 'e':
        m_keyboardInput.ElevateUpPressed = false;
        break;
    case 'C':
    case 'c':
        m_keyboardInput.ElevateDownPressed = false;
        break;             
    }
}