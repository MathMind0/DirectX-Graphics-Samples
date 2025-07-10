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
#include "D3D12SoftwareRasterization.h"

D3D12SoftwareRasterization::D3D12SoftwareRasterization(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_rtvDescriptorSize(0),
    m_srvUavDescriptorSize(0),
    m_currentFenceValue(0),
    m_frameFenceValues{}
{
    m_frameWidth = static_cast<UINT>(ceilf(static_cast<float>(m_width) / FrameScale));
    m_frameHeight = static_cast<UINT>(ceilf(static_cast<float>(m_height) / FrameScale));

    m_viewport = CD3DX12_VIEWPORT(0.f, 0.f,
        static_cast<float>(m_width), static_cast<float>(m_height));
    m_scissorRect = CD3DX12_RECT(0, 0, m_width, m_height);
    
    ThrowIfFailed(DXGIDeclareAdapterRemovalSupport());
}

void D3D12SoftwareRasterization::OnInit()
{
    m_camera.Init({ 0.0f, 0.0f, 5.0f });
    m_camera.SetMoveSpeed(250.0f);

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12SoftwareRasterization::LoadPipeline()
{
    // Create the factory and the device.
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
        
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_dxgiFactory)));

        if (m_useWarpDevice)
        {
            ComPtr<IDXGIAdapter> warpAdapter;
            ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

            ThrowIfFailed(D3D12CreateDevice(
                warpAdapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&m_device)
                ));
        }
        else
        {
            ComPtr<IDXGIAdapter1> hardwareAdapter;
            GetHardwareAdapter(m_dxgiFactory.Get(), &hardwareAdapter, true);

            ThrowIfFailed(D3D12CreateDevice(
                hardwareAdapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&m_device)
                ));
        }
    }

    // Create the command queues, the command allocators and the command lists.
    {
        // Create graphics resources.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueueGraphics)));
        NAME_D3D12_OBJECT(m_commandQueueGraphics);

        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocatorGraphics[n])));
            NAME_D3D12_OBJECT_INDEXED(m_commandAllocatorGraphics, n);
        }
        
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_commandAllocatorGraphics[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandListGraphics)));
        NAME_D3D12_OBJECT(m_commandListGraphics);
    }

    // Create the swap chain.
    {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(m_dxgiFactory->CreateSwapChainForHwnd(
            m_commandQueueGraphics.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            Win32Application::GetHwnd(),
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
            ));

        // This sample does not support fullscreen transitions.
        ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

        ThrowIfFailed(swapChain.As(&m_swapChain));
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    }

    // Create the descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a shader resource view (SRV) and unordered
        // access view (UAV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
        srvUavHeapDesc.NumDescriptors = DescriptorCount;
        srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvUavHeapDesc, IID_PPV_ARGS(&m_srvUavHeap)));
        NAME_D3D12_OBJECT(m_srvUavHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

// Load the sample assets.
void D3D12SoftwareRasterization::LoadAssets()
{
    CreateRTs();
    CreatePSOs();
    CreateMeshBuffers();
    CreateFrameBuffer();
    CreateConstantBuffer();

    // Close the command list and execute it to begin the initial GPU setup.
    ThrowIfFailed(m_commandListGraphics->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandListGraphics.Get() };
    m_commandQueueGraphics->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_currentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_renderContextFence)));
        m_currentFenceValue++;

        m_renderContextFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_renderContextFenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        FlushGPU();
    }
}

void D3D12SoftwareRasterization::CreateRTs()
{
    // Create RTV resources.
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
    for (UINT n = 0; n < FrameCount; n++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);
    }
}

void D3D12SoftwareRasterization::CreatePSOs()
{
    struct
    {
        byte* code;
        UINT size;
    } vs, ps, cs;
    
    ReadDataFromFile(GetAssetFullPath(L"CopyToRenderTargetVS.cso").c_str(),
                     &vs.code, &vs.size);
    ReadDataFromFile(GetAssetFullPath(L"CopyToRenderTargetPS.cso").c_str(),
                     &ps.code, &ps.size);

    ID3DBlob* rootSigBlob = nullptr;
    D3DGetBlobPart(ps.code, ps.size, D3D_BLOB_ROOT_SIGNATURE, 0, &rootSigBlob);
    ThrowIfFailed(m_device->CreateRootSignature(0,
                                                rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_sigCopyToRT)));
    NAME_D3D12_OBJECT(m_sigCopyToRT);
        
    // Describe and create the PSO for rendering the scene.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {nullptr, 0};
    psoDesc.pRootSignature = m_sigCopyToRT.Get();
    psoDesc.VS = {vs.code, vs.size};
    psoDesc.PS = {ps.code, ps.size};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.DepthStencilState.StencilEnable = false;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoCopyToRT)));
    NAME_D3D12_OBJECT(m_psoCopyToRT);
    
    ReadDataFromFile(GetAssetFullPath(L"RasterMain.cso").c_str(),
                 &cs.code, &cs.size);

    rootSigBlob->Release();
    D3DGetBlobPart(cs.code, cs.size, D3D_BLOB_ROOT_SIGNATURE, 0, &rootSigBlob);
    ThrowIfFailed(m_device->CreateRootSignature(0,
                                                rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_sigRasterize)));
    NAME_D3D12_OBJECT(m_sigRasterize);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDescRasterize = {};
    psoDescRasterize.pRootSignature = m_sigRasterize.Get();
    psoDescRasterize.CS = {cs.code, cs.size};
    
    ThrowIfFailed(m_device->CreateComputePipelineState(&psoDescRasterize, IID_PPV_ARGS(&m_psoRasterize)));
    NAME_D3D12_OBJECT(m_psoRasterize);

    ReadDataFromFile(GetAssetFullPath(L"RasterInit.cso").c_str(),
             &cs.code, &cs.size);

    rootSigBlob->Release();
    D3DGetBlobPart(cs.code, cs.size, D3D_BLOB_ROOT_SIGNATURE, 0, &rootSigBlob);
    ThrowIfFailed(m_device->CreateRootSignature(0,
                                                rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_sigRasterInit)));
    NAME_D3D12_OBJECT(m_sigRasterInit);
    
    psoDescRasterize.pRootSignature = m_sigRasterInit.Get();
    psoDescRasterize.CS = {cs.code, cs.size};
    
    ThrowIfFailed(m_device->CreateComputePipelineState(&psoDescRasterize, IID_PPV_ARGS(&m_psoRasterInit)));
    NAME_D3D12_OBJECT(m_psoRasterInit); 
}

// Create the vertex buffer and the index buffer of the mesh.
void D3D12SoftwareRasterization::CreateMeshBuffers()
{
    Vertex Vertices[] = {
    {{1.f, 0.f, 0.f}},
    {{-1.f, 0.f, 0.f}},
    {{0.f, 1.f, 0.f}}};

    size_t szVertexBuffer = sizeof(Vertices);
    
    uint32_t Indices[] = {0, 1, 2};
    size_t szIndexBuffer = sizeof(Indices);

    m_numTriangles = _countof(Indices) / 3;
    
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(szVertexBuffer),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(szVertexBuffer),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBufferUpload)));

    NAME_D3D12_OBJECT(m_vertexBuffer);

    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = Vertices;
    vertexData.RowPitch = szVertexBuffer;
    vertexData.SlicePitch = vertexData.RowPitch;

    UpdateSubresources<1>(m_commandListGraphics.Get(), m_vertexBuffer.Get(), m_vertexBufferUpload.Get(),
        0, 0, 1, &vertexData);
    m_commandListGraphics->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    m_srvVertexBufferCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)SRV_VERTEX_BUFFER, m_srvUavDescriptorSize);
    m_srvRasterCanvasGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)SRV_VERTEX_BUFFER, m_srvUavDescriptorSize);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvVertexBufferDesc = {};
    srvVertexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvVertexBufferDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvVertexBufferDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvVertexBufferDesc.Buffer.NumElements = _countof(Vertices);
    srvVertexBufferDesc.Buffer.StructureByteStride = sizeof(Vertex);

    m_device->CreateShaderResourceView(m_vertexBuffer.Get(), &srvVertexBufferDesc, m_srvVertexBufferCpu);

    ThrowIfFailed(m_device->CreateCommittedResource(
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
    D3D12_HEAP_FLAG_NONE,
    &CD3DX12_RESOURCE_DESC::Buffer(szIndexBuffer),
    D3D12_RESOURCE_STATE_COMMON,
    nullptr,
    IID_PPV_ARGS(&m_indexBuffer)));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(szIndexBuffer),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_indexBufferUpload)));

    NAME_D3D12_OBJECT(m_indexBuffer);

    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.pData = Indices;
    indexData.RowPitch = szIndexBuffer;
    indexData.SlicePitch = indexData.RowPitch;

    UpdateSubresources<1>(m_commandListGraphics.Get(), m_indexBuffer.Get(), m_indexBufferUpload.Get(),
        0, 0, 1, &indexData);
    m_commandListGraphics->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    m_srvIndexBufferCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)SRV_INDEX_BUFFER, m_srvUavDescriptorSize);
    m_srvRasterCanvasGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)SRV_INDEX_BUFFER, m_srvUavDescriptorSize);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvIndexBufferDesc = {};
    srvIndexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvIndexBufferDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvIndexBufferDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvIndexBufferDesc.Buffer.NumElements = m_numTriangles;
    srvIndexBufferDesc.Buffer.StructureByteStride = sizeof(uint32_t) * 3;

    m_device->CreateShaderResourceView(m_indexBuffer.Get(), &srvIndexBufferDesc, m_srvIndexBufferCpu);
}

void D3D12SoftwareRasterization::CreateFrameBuffer()
{
    CD3DX12_RESOURCE_DESC descFrameBuffer = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32_UINT,
        m_frameWidth,
        m_frameHeight,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &descFrameBuffer,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_texRasterCanvas)));

    NAME_D3D12_OBJECT(m_texRasterCanvas);

    m_srvRasterCanvasCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)SRV_FRAME_BUFFER, m_srvUavDescriptorSize);
    m_srvRasterCanvasGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)SRV_FRAME_BUFFER, m_srvUavDescriptorSize);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvRasterCanvasDesc = {};
    srvRasterCanvasDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvRasterCanvasDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvRasterCanvasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvRasterCanvasDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_texRasterCanvas.Get(), &srvRasterCanvasDesc, m_srvRasterCanvasCpu);

    m_uavRasterCanvasCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)UAV_FRAME_BUFFER, m_srvUavDescriptorSize);
    m_uavRasterCanvasGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)UAV_FRAME_BUFFER, m_srvUavDescriptorSize);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavRasterCanvasDesc = {};
    uavRasterCanvasDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavRasterCanvasDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavRasterCanvasDesc.Texture2D.MipSlice = 0;

    m_device->CreateUnorderedAccessView(m_texRasterCanvas.Get(), nullptr,
        &uavRasterCanvasDesc, m_uavRasterCanvasCpu);
}

void D3D12SoftwareRasterization::CreateConstantBuffer()
{
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    
    for (UINT i = 0; i < FrameCount; i++)
    {
        ThrowIfFailed(m_device->CreateCommittedResource(
             &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
             D3D12_HEAP_FLAG_NONE,
             &CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConstantBuffer)),
             D3D12_RESOURCE_STATE_GENERIC_READ,
             nullptr,
             IID_PPV_ARGS(&m_constantBuffer[i])));
        NAME_D3D12_OBJECT_INDEXED(m_constantBuffer, i);
        
        ThrowIfFailed(m_constantBuffer[i]->Map(0, &readRange,
            reinterpret_cast<void**>(&m_pConstantBufferData[i])));
        
        ZeroMemory(m_pConstantBufferData[i], sizeof(ConstantBuffer));
    }
}

// Update frame-based values.
void D3D12SoftwareRasterization::OnUpdate()
{
    m_timer.Tick(NULL);
    m_camera.Update(static_cast<float>(m_timer.GetElapsedSeconds()));

    ConstantBuffer constantBuffer = {};
    XMStoreFloat4x4(&constantBuffer.matMVP, XMMatrixMultiply(m_camera.GetViewMatrix(), m_camera.GetProjectionMatrix(0.8f, m_aspectRatio, 1.0f, 5000.0f)));
    constantBuffer.szCanvas.x = m_frameWidth;
    constantBuffer.szCanvas.y = m_frameHeight;
    constantBuffer.numTriangles.x = m_numTriangles;
    constantBuffer.numTriangles.y = 0;
    
    memcpy(m_pConstantBufferData[m_frameIndex], &constantBuffer, sizeof(ConstantBuffer));
}

// Render the scene.
void D3D12SoftwareRasterization::OnRender()
{
    try
    {
        PIXScopedEvent(m_commandQueueGraphics.Get(), 0, L"Render");

        // Record all the commands we need to render the scene into the command list.
        PopulateCommandList();

        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = { m_commandListGraphics.Get() };
        m_commandQueueGraphics->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // Present the frame.
        {
            PIXScopedEvent(m_commandQueueGraphics.Get(), 1, L"Present");
            ThrowIfFailed(m_swapChain->Present(1, 0));
        }

        {
            PIXScopedEvent(m_commandQueueGraphics.Get(), 2, L"MoveToNextFrame");
            MoveToNextFrame();    
        }        
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

// Release sample's D3D objects.
void D3D12SoftwareRasterization::ReleaseD3DResources()
{
    for (UINT n = 0; n < FrameCount; n++)
    {
        m_renderTargets[n].Reset();
    }

    m_psoRasterize.Reset();
    m_sigRasterize.Reset();
    m_psoRasterInit.Reset();
    m_sigRasterInit.Reset();
    m_psoCopyToRT.Reset();
    m_sigCopyToRT.Reset();
    
    m_vertexBuffer.Reset();
    m_vertexBufferUpload.Reset();
    m_indexBuffer.Reset();
    m_indexBufferUpload.Reset();
    
    m_rtvHeap.Reset();
    m_srvUavHeap.Reset();
    
    m_renderContextFence.Reset();
    ResetComPtrArray(&m_renderTargets);
    
    m_commandListGraphics.Reset();
    ResetComPtrArray(&m_commandAllocatorGraphics);
    m_commandQueueGraphics.Reset();
    
    m_swapChain.Reset();
    m_device.Reset();
    m_dxgiFactory.Reset();
}

// Tears down D3D resources and reinitializes them.
void D3D12SoftwareRasterization::RestoreD3DResources()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        FlushGPU();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    
    ReleaseD3DResources();
    OnInit();
}

// Fill the command list with all the render commands and dependent state.
void D3D12SoftwareRasterization::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocatorGraphics[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    ThrowIfFailed(m_commandListGraphics->Reset(m_commandAllocatorGraphics[m_frameIndex].Get(),
        m_psoRasterInit.Get()));
    
    ID3D12DescriptorHeap* ppHeaps[] = {m_srvUavHeap.Get()};
    m_commandListGraphics->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    {
        PIXScopedEvent(m_commandListGraphics.Get(), 0, L"RasterInit");

        m_commandListGraphics->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_texRasterCanvas.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        m_commandListGraphics->SetComputeRootSignature(m_sigRasterInit.Get());
        m_commandListGraphics->SetComputeRootConstantBufferView(0, m_constantBuffer[m_frameIndex]->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootShaderResourceView(1, m_vertexBuffer->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootShaderResourceView(2, m_indexBuffer->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootDescriptorTable(3, m_uavRasterCanvasGpu);
        m_commandListGraphics->Dispatch((m_frameWidth + 7)/ 8, (m_frameWidth + 7)/ 8, 1);

        m_commandListGraphics->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_texRasterCanvas.Get()));
    }

    {
        PIXScopedEvent(m_commandListGraphics.Get(), 0, L"Rasterization");

        m_commandListGraphics->SetPipelineState(m_psoRasterize.Get());
        m_commandListGraphics->SetComputeRootSignature(m_sigRasterize.Get());
        m_commandListGraphics->SetComputeRootConstantBufferView(0, m_constantBuffer[m_frameIndex]->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootShaderResourceView(1, m_vertexBuffer->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootShaderResourceView(2, m_indexBuffer->GetGPUVirtualAddress());
        m_commandListGraphics->SetComputeRootDescriptorTable(3, m_uavRasterCanvasGpu);
        m_commandListGraphics->Dispatch(1, 1, 1);

        m_commandListGraphics->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_texRasterCanvas.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }

    {
        PIXScopedEvent(m_commandListGraphics.Get(), 0, L"CopyToRT");
        
        // Indicate that the back buffer will be used as a render target.
        m_commandListGraphics->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_frameIndex, m_rtvDescriptorSize);
        m_commandListGraphics->OMSetRenderTargets(1,
            &rtvHandle, FALSE, nullptr);
        m_commandListGraphics->RSSetViewports(1, &m_viewport);
        m_commandListGraphics->RSSetScissorRects(1, &m_scissorRect);
        
        const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_commandListGraphics->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        m_commandListGraphics->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandListGraphics->IASetVertexBuffers(0, 0, nullptr);
        m_commandListGraphics->IASetIndexBuffer(nullptr);
        
        m_commandListGraphics->SetPipelineState(m_psoCopyToRT.Get());
        m_commandListGraphics->SetGraphicsRootSignature(m_sigCopyToRT.Get());
        UINT szCanvas[] = { m_frameWidth, m_frameHeight };
        m_commandListGraphics->SetGraphicsRoot32BitConstants(0, 2, static_cast<void*>(szCanvas), 0);
        m_commandListGraphics->SetGraphicsRootDescriptorTable(1, m_srvRasterCanvasGpu);

        m_commandListGraphics->DrawInstanced(3, 1, 0, 0);

        m_commandListGraphics->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    }
    
    ThrowIfFailed(m_commandListGraphics->Close());
}

void D3D12SoftwareRasterization::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    FlushGPU();

    // Close handles to fence events and threads.
    CloseHandle(m_renderContextFenceEvent);
}

void D3D12SoftwareRasterization::OnKeyDown(UINT8 key)
{
    m_camera.OnKeyDown(key);
}

void D3D12SoftwareRasterization::OnKeyUp(UINT8 key)
{
    m_camera.OnKeyUp(key);
}

void D3D12SoftwareRasterization::FlushGPU()
{
    // Add a signal command to the queue.
    ThrowIfFailed(m_commandQueueGraphics->Signal(m_renderContextFence.Get(), m_currentFenceValue));

    // Instruct the fence to set the event object when the signal command completes.
    ThrowIfFailed(m_renderContextFence->SetEventOnCompletion(m_currentFenceValue, m_renderContextFenceEvent));
    m_currentFenceValue++;

    // Wait until the signal command has been processed.
    WaitForSingleObject(m_renderContextFenceEvent, INFINITE);
}

// Cycle through the frame resources. This method blocks execution if the 
// next frame resource in the queue has not yet had its previous contents 
// processed by the GPU.
void D3D12SoftwareRasterization::MoveToNextFrame()
{
    // Assign the current fence value to the current frame.
    m_frameFenceValues[m_frameIndex] = m_currentFenceValue;
    
    // Signal and increment the fence value.
    ThrowIfFailed(m_commandQueueGraphics->Signal(m_renderContextFence.Get(), m_currentFenceValue));
    m_currentFenceValue++;

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_renderContextFence->GetCompletedValue() < m_frameFenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_renderContextFence->SetEventOnCompletion(m_frameFenceValues[m_frameIndex], m_renderContextFenceEvent));
        WaitForSingleObject(m_renderContextFenceEvent, INFINITE);
    }
}
