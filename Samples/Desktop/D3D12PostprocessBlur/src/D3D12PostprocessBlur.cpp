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
#include "D3D12PostprocessBlur.h"
#include "FrameResource.h"

D3D12PostprocessBlur* D3D12PostprocessBlur::s_app = nullptr;

D3D12PostprocessBlur::D3D12PostprocessBlur(UINT width, UINT height, std::wstring name) :
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

D3D12PostprocessBlur::~D3D12PostprocessBlur()
{
    s_app = nullptr;
}

void D3D12PostprocessBlur::OnInit()
{
    CreateRenderContext();
    CreateSceneResources();
}

// Load the rendering pipeline dependencies.
void D3D12PostprocessBlur::CreateRenderContext()
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

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    m_compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    m_compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
}

void D3D12PostprocessBlur::CreateDescriptorHeaps()
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

    // Describe and create a shader resource view (SRV) and constant 
    // buffer view (CBV) descriptor heap.  
    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = (UINT)CSU_DESCRIPTORS::NUM_DESCRIPTORS +
        (UINT)FRAME_CSU_DESCRIPTORS::NUM_DESCRIPTORS * FrameCount;
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

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    m_defaultDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3D12PostprocessBlur::CreateFrameResources()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    ComPtr<ID3D12Resource> backBuffer;
    
    for (UINT i = 0; i < FrameCount; i++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        m_frameResources[i] = new FrameResource(m_device.Get(),
            m_cbvSrvHeap.Get(), m_defaultDescriptorSize,
            backBuffer.Get(), rtvHandle, i);
        
        m_frameResources[i]->WriteConstantBuffers(m_viewport, &m_camera,
            m_lightCameras, m_lights, NumLights);
        
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        NAME_D3D12_OBJECT(backBuffer);
    }
}

void D3D12PostprocessBlur::CreateSceneSignatures()
{
    CD3DX12_DESCRIPTOR_RANGE1 ranges[4]; // Perfomance TIP: Order from most frequent to least frequent.
    // 2 frequently changed diffuse + normal textures - using registers t1 and t2.
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    // 1 frequently changed constant buffer.
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    // 1 infrequently changed shadow texture - starting in register t0.
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    // 2 static samplers.
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0);                                            

    CD3DX12_ROOT_PARAMETER1 rootParameters[4];
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters,
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

    // Create root signature for postprocess blur.
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[1].InitAsConstantBufferView(0, 0,
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignatureDesc.Init_1_1(2, rootParameters, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, m_featureData.HighestVersion,
        &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_sigBlur)));
    NAME_D3D12_OBJECT(m_sigBlur);
}

void D3D12PostprocessBlur::CreateScenePSOs()
{
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> error;
    
    ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
        "VSMain", "vs_5_0", m_compileFlags, 0, &vertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
        "PSMain", "ps_5_0", m_compileFlags, 0, &pixelShader, nullptr));

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc;
    inputLayoutDesc.pInputElementDescs = SampleAssets::StandardVertexDescription;
    inputLayoutDesc.NumElements = _countof(SampleAssets::StandardVertexDescription);

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencilDesc.StencilEnable = FALSE;

    // Describe and create the PSO for rendering the scene.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;
    psoDesc.pRootSignature = m_sigRenderScene.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoRenderScene)));
    NAME_D3D12_OBJECT(m_psoRenderScene);

    // Alter the description and create the PSO for rendering
    // the shadow map.  The shadow map does not use a pixel
    // shader or render targets.
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(0, 0);
    psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    psoDesc.NumRenderTargets = 0;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoRenderShadow)));
    NAME_D3D12_OBJECT(m_psoRenderShadow);

    // Create postprocess blur PSO.
    vertexShader = CompileShader(GetAssetFullPath(L"Postprocess.hlsl").c_str(), nullptr,
        "VSPostprocess", "vs_5_1");
    pixelShader = CompileShader(GetAssetFullPath(L"Postprocess.hlsl").c_str(), nullptr,
    "PSPostprocessBlurNaive", "ps_5_1");

    psoDesc.InputLayout.pInputElementDescs = nullptr; psoDesc.InputLayout.NumElements = 0;
    psoDesc.pRootSignature = m_sigBlur.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoBlur)));
    NAME_D3D12_OBJECT(m_psoBlur);
}

void D3D12PostprocessBlur::CreateDepthBuffer()
{
    CD3DX12_RESOURCE_DESC depthBufferDesc(
            D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            0,
            static_cast<UINT>(m_viewport.Width), 
            static_cast<UINT>(m_viewport.Height), 
            1,
            1,
            DXGI_FORMAT_D32_FLOAT,
            1, 
            0,
            D3D12_TEXTURE_LAYOUT_UNKNOWN,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

    D3D12_CLEAR_VALUE clearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
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

void D3D12PostprocessBlur::CreateSceneAssets()
{
    // Load scene assets.
    UINT fileSize = 0;
    UINT8* pAssetData;
    ThrowIfFailed(ReadDataFromFile(
        GetAssetFullPath(SampleAssets::DataFileName).c_str(),
        &pAssetData, &fileSize));

    LoadAssetVertexBuffer(pAssetData);
    LoadAssetIndexBuffer(pAssetData);
    LoadAssetTextures(pAssetData);
    
    free(pAssetData);
}

void D3D12PostprocessBlur::LoadAssetVertexBuffer(const UINT8* pAssetData)
{
    ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::VertexDataSize),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

    NAME_D3D12_OBJECT(m_vertexBuffer);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::VertexDataSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBufferUpload)));

    // Copy data to the upload heap and then schedule a copy 
    // from the upload heap to the vertex buffer.
    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = pAssetData + SampleAssets::VertexDataOffset;
    vertexData.RowPitch = SampleAssets::VertexDataSize;
    vertexData.SlicePitch = vertexData.RowPitch;

    PIXBeginEvent(m_commandList.Get(), 0, L"Copy vertex buffer data to default resource...");

    UpdateSubresources<1>(m_commandList.Get(),
        m_vertexBuffer.Get(), m_vertexBufferUpload.Get(),
        0, 0, 1, &vertexData);
        
    m_commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

    PIXEndEvent(m_commandList.Get());

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = SampleAssets::VertexDataSize;
    m_vertexBufferView.StrideInBytes = SampleAssets::StandardVertexStride;
}

void D3D12PostprocessBlur::LoadAssetIndexBuffer(const UINT8* pAssetData)
{
    ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_indexBuffer)));

    NAME_D3D12_OBJECT(m_indexBuffer);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_indexBufferUpload)));

    // Copy data to the upload heap and then schedule a copy 
    // from the upload heap to the index buffer.
    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.pData = pAssetData + SampleAssets::IndexDataOffset;
    indexData.RowPitch = SampleAssets::IndexDataSize;
    indexData.SlicePitch = indexData.RowPitch;

    PIXBeginEvent(m_commandList.Get(), 0, L"Copy index buffer data to default resource...");

    UpdateSubresources<1>(m_commandList.Get(),
        m_indexBuffer.Get(), m_indexBufferUpload.Get(),
        0, 0, 1, &indexData);
    m_commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));

    PIXEndEvent(m_commandList.Get());

    // Initialize the index buffer view.
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = SampleAssets::IndexDataSize;
    m_indexBufferView.Format = SampleAssets::StandardIndexFormat;
}

void D3D12PostprocessBlur::LoadAssetTextures(const UINT8* pAssetData)
{
    m_srvNullGPU = m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
    m_srvFirstTextureGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        2, m_defaultDescriptorSize);
    
    // Create shader resources.
    {
        // Get a handle to the start of the descriptor heap.
        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

        {
            // Describe and create 2 null SRVs. Null descriptors are needed in order 
            // to achieve the effect of an "unbound" resource.
            D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
            nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            nullSrvDesc.Texture2D.MipLevels = 1;
            nullSrvDesc.Texture2D.MostDetailedMip = 0;
            nullSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            m_device->CreateShaderResourceView(nullptr, &nullSrvDesc, cbvSrvHandle);
            cbvSrvHandle.Offset(m_defaultDescriptorSize);

            m_device->CreateShaderResourceView(nullptr, &nullSrvDesc, cbvSrvHandle);
            cbvSrvHandle.Offset(m_defaultDescriptorSize);
        }

        // Create each texture and SRV descriptor.
        const UINT srvCount = _countof(SampleAssets::Textures);
        PIXBeginEvent(m_commandList.Get(), 0,
            L"Copy diffuse and normal texture data to default resources...");
        for (UINT i = 0; i < srvCount; i++)
        {
            // Describe and create a Texture2D.
            const SampleAssets::TextureResource &tex = SampleAssets::Textures[i];
            CD3DX12_RESOURCE_DESC texDesc(
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                0,
                tex.Width, 
                tex.Height, 
                1,
                static_cast<UINT16>(tex.MipLevels),
                tex.Format,
                1, 
                0,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_FLAG_NONE);

            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_textures[i])));

            NAME_D3D12_OBJECT_INDEXED(m_textures, i);

            {
                const UINT subresourceCount = texDesc.DepthOrArraySize * texDesc.MipLevels;
                UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_textures[i].Get(),
                    0, subresourceCount);
                ThrowIfFailed(m_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&m_textureUploads[i])));

                // Copy data to the intermediate upload heap and then schedule a copy
                // from the upload heap to the Texture2D.
                D3D12_SUBRESOURCE_DATA textureData = {};
                textureData.pData = pAssetData + tex.Data->Offset;
                textureData.RowPitch = tex.Data->Pitch;
                textureData.SlicePitch = tex.Data->Size;

                UpdateSubresources(m_commandList.Get(),
                    m_textures[i].Get(), m_textureUploads[i].Get(),
                    0, 0, subresourceCount, &textureData);
                m_commandList->ResourceBarrier(1,
                    &CD3DX12_RESOURCE_BARRIER::Transition(m_textures[i].Get(),
                        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
            }

            // Describe and create an SRV.
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = tex.Format;
            srvDesc.Texture2D.MipLevels = tex.MipLevels;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            m_device->CreateShaderResourceView(m_textures[i].Get(), &srvDesc, cbvSrvHandle);

            // Move to the next descriptor slot.
            cbvSrvHandle.Offset(m_defaultDescriptorSize);
        }
        
        PIXEndEvent(m_commandList.Get());
    }
}

void D3D12PostprocessBlur::CreateSamplers()
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

void D3D12PostprocessBlur::CreateLights()
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
        XMVECTOR at = XMVectorAdd(eye, XMLoadFloat4(&m_lights[i].direction));
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        m_lightCameras[i].Set(eye, at, up);
    }    
}

void D3D12PostprocessBlur::CreateSyncFence()
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

void D3D12PostprocessBlur::CreateShadowResources()
{
    // Describe and create the shadow map texture.
    CD3DX12_RESOURCE_DESC shadowTexDesc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        static_cast<UINT>(m_viewport.Width), 
        static_cast<UINT>(m_viewport.Height), 
        1,
        1,
        DXGI_FORMAT_R32_TYPELESS,
        1, 
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clearValue; // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadowTexDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowTexture)));

    NAME_D3D12_OBJECT(m_shadowTexture);

    // Get a handle to the start of the descriptor heap then offset 
    // it based on the frame resource index.
    CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)DSV_DESCRIPTORS::SHADOW_DSV, m_dsvDescriptorSize); // + 1 for the shadow map.

    // Describe and create the shadow depth view and cache the CPU 
    // descriptor handle.
    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
    depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Texture2D.MipSlice = 0;
    m_device->CreateDepthStencilView(m_shadowTexture.Get(), &depthStencilViewDesc, depthHandle);
    m_shadowDepthView = depthHandle;
    
    // Describe and create a shader resource view (SRV) for the shadow depth 
    // texture and cache the GPU descriptor handle. This SRV is for sampling 
    // the shadow map from our shader. It uses the same texture that we use 
    // as a depth-stencil during the shadow pass.
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvShadowCPU(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)CSU_DESCRIPTORS::SHADOW_SRV, m_defaultDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvShadowGPU(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)CSU_DESCRIPTORS::SHADOW_SRV, m_defaultDescriptorSize);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
    shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowSrvDesc.Texture2D.MipLevels = 1;
    shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_device->CreateShaderResourceView(m_shadowTexture.Get(), &shadowSrvDesc, srvShadowCPU);
    m_shadowDepthHandle = srvShadowGPU;
}

void D3D12PostprocessBlur::CreatePostprocessResources()
{
    // Resource initialization for postprocess.
    CD3DX12_RESOURCE_DESC descSceneColor = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        static_cast<UINT>(m_viewport.Width),
        static_cast<UINT>(m_viewport.Height),
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    FLOAT clearColor[] = {0.f, 0.f, 0.f, 1.f};
    CD3DX12_CLEAR_VALUE clearSceneColor(DXGI_FORMAT_R8G8B8A8_UNORM,
        clearColor);
        
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &descSceneColor,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearSceneColor,
        IID_PPV_ARGS(&m_texSceneColor)));

    NAME_D3D12_OBJECT(m_texSceneColor);

    m_srvSceneColorCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
    (INT)CSU_DESCRIPTORS::SCREEN_COLOR_SRV, m_defaultDescriptorSize);
    m_srvSceneColorGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)CSU_DESCRIPTORS::SCREEN_COLOR_SRV, m_defaultDescriptorSize);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvSceneColorDesc = {};
    srvSceneColorDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvSceneColorDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvSceneColorDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvSceneColorDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_texSceneColor.Get(), &srvSceneColorDesc, m_srvSceneColorCpu);
    
    m_rtvSceneColorCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)RTV_DESCRIPTORS::SCREEN_COLOR_RTV, m_rtvDescriptorSize);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_UNKNOWN;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    m_device->CreateRenderTargetView(m_texSceneColor.Get(), &rtvDesc, m_rtvSceneColorCpu);
}

// Load the sample assets.
void D3D12PostprocessBlur::CreateSceneResources()
{
    CreateDescriptorHeaps();
    CreateDepthBuffer();
    CreateSceneSignatures();
    CreateScenePSOs();
    CreateSamplers();
    CreateSceneAssets();    
    CreateLights();
    CreateShadowResources();
    CreatePostprocessResources();

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
void D3D12PostprocessBlur::OnUpdate()
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

    if (m_keyboardInput.leftArrowPressed)
        m_camera.RotateYaw(-frameChange);
    if (m_keyboardInput.rightArrowPressed)
        m_camera.RotateYaw(frameChange);
    if (m_keyboardInput.upArrowPressed)
        m_camera.RotatePitch(frameChange);
    if (m_keyboardInput.downArrowPressed)
        m_camera.RotatePitch(-frameChange);

    if (m_keyboardInput.animate)
    {
        for (int i = 0; i < NumLights; i++)
        {
            float direction = frameChange * powf(-1.0f, static_cast<float>(i));
            XMStoreFloat4(&m_lights[i].position, XMVector4Transform(XMLoadFloat4(&m_lights[i].position), XMMatrixRotationY(direction)));

            XMVECTOR eye = XMLoadFloat4(&m_lights[i].position);
            XMVECTOR at = XMVectorSet(0.0f, 8.0f, 0.0f, 0.0f);
            XMStoreFloat4(&m_lights[i].direction, XMVector3Normalize(XMVectorSubtract(at, eye)));
            XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            m_lightCameras[i].Set(eye, at, up);

            m_lightCameras[i].Get3DViewProjMatrices(&m_lights[i].view, &m_lights[i].projection, 90.0f, static_cast<float>(m_width), static_cast<float>(m_height));
        }
    }

    m_pCurrentFrameResource->WriteConstantBuffers(m_viewport, &m_camera, m_lightCameras, m_lights, NumLights);
}

// Render the scene.
void D3D12PostprocessBlur::OnRender()
{
    try
    {
        ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
                
        BeginFrame();
        RenderShadow();
        RenderScene();
        RenderPostprocess();
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

void D3D12PostprocessBlur::BeginFrame()
{    
    // Reset the command allocator and list.
    ThrowIfFailed(m_pCurrentFrameResource->commandAllocator->Reset());
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    ThrowIfFailed(commandList->Reset(m_pCurrentFrameResource->commandAllocator.Get(), nullptr));

    PIXBeginEvent(commandList, 0, L"Rendering frame begin ...");
    
    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get(), m_samplerHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
}

void D3D12PostprocessBlur::EndFrame()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    PIXEndEvent(commandList);
    ThrowIfFailed(commandList->Close());

    ID3D12CommandList* commandLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
}

void D3D12PostprocessBlur::RenderShadow()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();

    PIXBeginEvent(commandList, 0, L"Rendering shadow pass...");
    
    // Shadow pass. We use constant buf #1 and depth stencil #1
    // with rendering to the render target disabled.    
    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE));
    
    // Clear the depth stencil buffer in preparation for rendering the shadow map.
    commandList->ClearDepthStencilView(m_shadowDepthView, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(0, nullptr,
        FALSE, &m_shadowDepthView);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    commandList->SetPipelineState(m_psoRenderShadow.Get());
    commandList->SetGraphicsRootSignature(m_sigRenderScene.Get());
    // Set null SRVs for the diffuse/normal textures.
    commandList->SetGraphicsRootDescriptorTable(0, m_srvNullGPU);    
    commandList->SetGraphicsRootDescriptorTable(1, m_pCurrentFrameResource->cbvShadow);
    // Set a null SRV for the shadow texture.
    commandList->SetGraphicsRootDescriptorTable(2, m_srvNullGPU);      
    commandList->SetGraphicsRootDescriptorTable(3, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
    
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);
    
    for (const SampleAssets::DrawParameters& drawArgs : SampleAssets::Draws)
    {
        commandList->DrawIndexedInstanced(
            drawArgs.IndexCount, 1,
            drawArgs.IndexStart, drawArgs.VertexBase,
            0);
    }

    PIXEndEvent(commandList);
}

void D3D12PostprocessBlur::RenderScene()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    
    PIXBeginEvent(commandList, 0, L"Rendering scene pass...");
    
    // Scene pass. We use constant buf #2 and depth stencil #2
    // with rendering to the render target enabled.
    
    // Transition the shadow map from writeable to readable.
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_shadowTexture.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_texSceneColor.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    
    commandList->ResourceBarrier(_countof(barriers), barriers);

    // Clear the render target and depth stencil.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(m_rtvSceneColorCpu, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(m_dsvDepthStencil, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(1, &m_rtvSceneColorCpu,
        FALSE, &m_dsvDepthStencil);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);
    
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);

    commandList->SetPipelineState(m_psoRenderScene.Get());
    commandList->SetGraphicsRootSignature(m_sigRenderScene.Get());
    commandList->SetGraphicsRootDescriptorTable(1, m_pCurrentFrameResource->cbvScene);
    commandList->SetGraphicsRootDescriptorTable(2, m_shadowDepthHandle); // Set the shadow texture as an SRV.
    commandList->SetGraphicsRootDescriptorTable(3, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
    
    for (const SampleAssets::DrawParameters& drawArgs : SampleAssets::Draws)
    {
        // Set the diffuse and normal textures for the current object.
        CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_srvFirstTextureGPU,
            drawArgs.DiffuseTextureIndex, m_defaultDescriptorSize);
        commandList->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);

        commandList->DrawIndexedInstanced(drawArgs.IndexCount, 1,
            drawArgs.IndexStart, drawArgs.VertexBase, 0);
    }
    
    PIXEndEvent(commandList);
}

void D3D12PostprocessBlur::RenderPostprocess()
{
    ID3D12GraphicsCommandList* commandList = m_pCurrentFrameResource->commandList.Get();
    
    PIXBeginEvent(commandList, 0, L"Rendering postprocess blur pass...");

    // Indicate that the back buffer will be used as a render target.
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrentFrameResource->backBuffer.Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_texSceneColor.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };

    commandList->ResourceBarrier(_countof(barriers), barriers);

    commandList->OMSetRenderTargets(1, &m_pCurrentFrameResource->rtvBackBuffer,
        FALSE, nullptr);

    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);
    
    commandList->SetGraphicsRootSignature(m_sigBlur.Get());
    commandList->SetPipelineState(m_psoBlur.Get());
    commandList->SetGraphicsRootDescriptorTable(0, m_srvSceneColorGpu);
    commandList->SetGraphicsRootConstantBufferView(1,
        m_pCurrentFrameResource->cbScreenInfo->GetGPUVirtualAddress());

    commandList->DrawInstanced(3, 1, 0, 0);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_pCurrentFrameResource->backBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    PIXEndEvent(commandList);
}

// Release sample's D3D objects.
void D3D12PostprocessBlur::ReleaseD3DResources()
{
    m_fence.Reset();
    m_commandQueue.Reset();
    m_swapChain.Reset();
    m_device.Reset();
}

// Tears down D3D resources and reinitializes them.
void D3D12PostprocessBlur::RestoreD3DResources()
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
void D3D12PostprocessBlur::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
}

void D3D12PostprocessBlur::OnDestroy()
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
}

void D3D12PostprocessBlur::OnKeyDown(UINT8 key)
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
    case VK_SPACE:
        m_keyboardInput.animate = !m_keyboardInput.animate;
        break;
    }
}

void D3D12PostprocessBlur::OnKeyUp(UINT8 key)
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
    }
}