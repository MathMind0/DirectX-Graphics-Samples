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
#include "D3D12ExecuteIndirect.h"

D3D12ExecuteIndirect::D3D12ExecuteIndirect(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_cullingScissorRect(),
    m_rtvDescriptorSize(0),
    m_cbvSrvUavDescriptorSize(0),
    m_csRootConstants(),
    m_enableCulling(true),
    m_fenceValues{}
{
    m_constantBufferData.resize(TriangleCount);
    
    m_lastFrameTime = high_resolution_clock::now();
    
    m_csRootConstants.cullBox = CullBox;
    m_csRootConstants.commandCount = (float)TriangleCount;
    m_csRootConstants.deltaTime = 0.f;

    m_viewDataCB.ratio = static_cast<float>(width) / static_cast<float>(height);

    const float centerX = width / 2.0f;
    m_cullingScissorRect.left = static_cast<LONG>(centerX + centerX * CullBox.x);
    m_cullingScissorRect.right = static_cast<LONG>(centerX + centerX * CullBox.z);
    const float centerY = height / 2.0f;
    m_cullingScissorRect.top = static_cast<LONG>(centerY - centerY * CullBox.w);
    m_cullingScissorRect.bottom = static_cast<LONG>(centerY - centerY * CullBox.y);

    ThrowIfFailed(DXGIDeclareAdapterRemovalSupport());
}

void D3D12ExecuteIndirect::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12ExecuteIndirect::LoadPipeline()
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

    // Describe and create the command queues.
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
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    
    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        // Describe and create a constant buffer view (CBV), Shader resource
        // view (SRV), and unordered access view (UAV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
        cbvSrvUavHeapDesc.NumDescriptors = CbvSrvUavDescriptorCountPerFrame;
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap)));
        NAME_D3D12_OBJECT(m_cbvSrvUavHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and command allocators for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }
}

// Load the sample assets.
void D3D12ExecuteIndirect::LoadAssets()
{
    // Create the root signatures.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion
        // returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
            &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount];
        rootParameters[StaticData].InitAsConstantBufferView(0, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[DynmicData].InitAsConstants(GRAPHICS_DYNAMIC_DATA_CONST_VALUES, 1,0,
            D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[ViewData].InitAsConstants(GRAPHICS_VIEW_DATA_CONST_VALUES, 2,0,
            D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
        NAME_D3D12_OBJECT(m_rootSignature);

        // Create compute signature.
        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

        CD3DX12_ROOT_PARAMETER1 computeRootParameters[ComputeRootParametersCount];
        computeRootParameters[SrvUavTable].InitAsDescriptorTable(2, ranges);
        computeRootParameters[RootConstants].InitAsConstants(COMPUTE_CONST_VALUE_SLOTS, 0);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
        computeRootSignatureDesc.Init_1_1(_countof(computeRootParameters), computeRootParameters);

        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature)));
        NAME_D3D12_OBJECT(m_computeRootSignature);
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> computeShader;
        ComPtr<ID3DBlob> error;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailedWithError(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
            "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error),
            "VSMain", &error);
        ThrowIfFailedWithError(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
            "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error),
            "PSMain", &error);
        ThrowIfFailedWithError(D3DCompileFromFile(GetAssetFullPath(L"compute.hlsl").c_str(), nullptr, nullptr,
            "CSMain", "cs_5_0", compileFlags, 0, &computeShader, &error),
            "CSMain", &error);

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state objects (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
        NAME_D3D12_OBJECT(m_pipelineState);

        // Describe and create the compute pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = m_computeRootSignature.Get();
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

        ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computeState)));
        NAME_D3D12_OBJECT(m_computeState);
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    NAME_D3D12_OBJECT(m_commandList);

    // Note: ComPtr's are CPU objects but these resources need to stay in scope until
    // the command list that references them has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resources are not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> vertexBufferUpload;
    ComPtr<ID3D12Resource> commandBufferUpload;

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { { 0.0f, TriangleHalfWidth, 0.f } },
            { { TriangleHalfWidth, -TriangleHalfWidth, 0.f } },
            { { -TriangleHalfWidth, -TriangleHalfWidth, 0.f } }
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBufferUpload)));

        NAME_D3D12_OBJECT(m_vertexBuffer);

        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the vertex buffer.
        D3D12_SUBRESOURCE_DATA vertexData = {};
        vertexData.pData = reinterpret_cast<UINT8*>(triangleVertices);
        vertexData.RowPitch = vertexBufferSize;
        vertexData.SlicePitch = vertexData.RowPitch;

        UpdateSubresources<1>(m_commandList.Get(), m_vertexBuffer.Get(),
            vertexBufferUpload.Get(), 0, 0, 1, &vertexData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = sizeof(triangleVertices);
    }

    // Create the depth stencil view.
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height,
                1, 0, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&m_depthStencil)
            ));

        NAME_D3D12_OBJECT(m_depthStencil);

        m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc,
            m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create the constant buffers.
    {
        const UINT constantBufferDataSize = TriangleCount * sizeof(PrimitiveStaticDataCB);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(constantBufferDataSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        NAME_D3D12_OBJECT(m_constantBuffer);

        // Initialize the constant buffers for each of the triangles.
        for (UINT n = 0; n < TriangleCount; n++)
        {
            m_constantBufferData[n].color = XMFLOAT4(GetRandomFloat(0.0f, 1.0f), GetRandomFloat(0.0f, 1.0f),
                GetRandomFloat(0.0f, 1.0f), 1.0f);
            m_constantBufferData[n].velocity = XMFLOAT2(GetRandomFloat(-0.5f, 0.5f), GetRandomFloat(-0.5f, 0.5f));
            m_constantBufferData[n].size = GetRandomFloat(1.0f, 10.0f);
            m_constantBufferData[n].rotation = GetRandomFloat(-1.f * XM_2PI, 1.f * XM_2PI);
        }

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData[0], constantBufferDataSize);

        // Create shader resource views (SRV) of the constant buffers for the
        // compute shader to read from.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = TriangleCount;
        srvDesc.Buffer.StructureByteStride = sizeof(PrimitiveStaticDataCB);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), CbvSrvOffset,
            m_cbvSrvUavDescriptorSize);
        m_device->CreateShaderResourceView(m_constantBuffer.Get(), &srvDesc, cbvSrvHandle);
    }

    // Create the command signature used for indirect drawing.
    {
        // Each command consists of a CBV update and a DrawInstanced call.
        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
        argumentDescs[0].ConstantBufferView.RootParameterIndex = StaticData;
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDescs[1].Constant.RootParameterIndex = DynmicData;
        argumentDescs[1].Constant.Num32BitValuesToSet = GRAPHICS_DYNAMIC_DATA_CONST_VALUES;
        argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
        argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.pArgumentDescs = argumentDescs;
        commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
        commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

        ThrowIfFailed(m_device->CreateCommandSignature(&commandSignatureDesc, m_rootSignature.Get(),
            IID_PPV_ARGS(&m_commandSignature)));
        NAME_D3D12_OBJECT(m_commandSignature);
    }

    // Create the command buffers and UAVs to store the results of the compute work.
    {
        std::vector<IndirectCommand> commands;
        commands.resize(TriangleCount);

        D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
            TriangleCount * sizeof(IndirectCommand),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &commandBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_commandBuffer)));

        commandBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &commandBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&commandBufferUpload)));

        NAME_D3D12_OBJECT(m_commandBuffer);

        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_constantBuffer->GetGPUVirtualAddress();
        
        for (UINT commandIndex = 0; commandIndex < TriangleCount; commandIndex++)
        {
            commands[commandIndex].cbStaticData = gpuAddress;
            commands[commandIndex].cbDynamicData.angle = 0.f;
            commands[commandIndex].cbDynamicData.position = XMFLOAT2(
                GetRandomFloat(-1.0f, 1.0f), GetRandomFloat(-1.0f, 1.0f));
            commands[commandIndex].drawArguments.VertexCountPerInstance = 3;
            commands[commandIndex].drawArguments.InstanceCount = 1;
            commands[commandIndex].drawArguments.StartVertexLocation = 0;
            commands[commandIndex].drawArguments.StartInstanceLocation = 0;
            gpuAddress += sizeof(PrimitiveStaticDataCB);
        }
        
        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the command buffer.
        D3D12_SUBRESOURCE_DATA commandData = {};
        commandData.pData = reinterpret_cast<UINT8*>(&commands[0]);
        commandData.RowPitch = commands.size();
        commandData.SlicePitch = commandData.RowPitch;

        UpdateSubresources<1>(m_commandList.Get(), m_commandBuffer.Get(),
            commandBufferUpload.Get(), 0, 0, 1, &commandData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_commandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = TriangleCount;
        uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
            CommandsOffset, m_cbvSrvUavDescriptorSize);
        m_device->CreateUnorderedAccessView(m_commandBuffer.Get(), nullptr, &uavDesc, commandsHandle);

        // Create the unordered access views (UAVs) that store the results of the compute work.
        CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
            ProcessedCommandsOffset, m_cbvSrvUavDescriptorSize);

        // Allocate a buffer large enough to hold all of the indirect commands
        // for a single frame as well as a UAV counter.
        commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
            CommandBufferCounterOffset + sizeof(UINT),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &commandBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_processedCommandBuffer)));

        NAME_D3D12_OBJECT(m_processedCommandBuffer);
        
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = TriangleCount;
        uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
        uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        m_device->CreateUnorderedAccessView(
            m_processedCommandBuffer.Get(),
            m_processedCommandBuffer.Get(),
            &uavDesc,
            processedCommandsHandle);

        // Allocate a buffer that can be used to reset the UAV counters and initialize
        // it to 0.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_processedCommandBufferCounterReset)));

        UINT8* pMappedCounterReset = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_processedCommandBufferCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset)));
        ZeroMemory(pMappedCounterReset, sizeof(UINT));
        m_processedCommandBufferCounterReset->Unmap(0, nullptr);
    }

    // Close the command list and execute it to begin the vertex buffer copy into
    // the default heap.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForGpu();
    }
}

// Get a random float value between min and max.
float D3D12ExecuteIndirect::GetRandomFloat(float min, float max)
{
    float scale = static_cast<float>(rand()) / RAND_MAX;
    float range = max - min;
    return scale * range + min;
}

// Update frame-based values.
void D3D12ExecuteIndirect::OnUpdate()
{
    time_point<high_resolution_clock> now = high_resolution_clock::now();
    duration<float> frame_time = now - m_lastFrameTime;
    m_csRootConstants.deltaTime = frame_time.count();
    m_lastFrameTime = now;
    SetCustomWindowText((std::to_wstring(m_csRootConstants.deltaTime) + L"ms").c_str());
}

// Render the scene.
void D3D12ExecuteIndirect::OnRender()
{
    try
    {
        // Record all the commands we need to render the scene into the command list.
        PopulateCommandLists();
        
        PIXBeginEvent(m_commandQueue.Get(), 0, L"Render");

        // Execute the rendering work.
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        PIXEndEvent(m_commandQueue.Get());

        // Present the frame.
        ThrowIfFailed(m_swapChain->Present(1, 0));

        MoveToNextFrame();
    }
    catch (HrException& e)
    {
        throw e;
#if 0
        if (e.Error() == DXGI_ERROR_DEVICE_REMOVED || e.Error() == DXGI_ERROR_DEVICE_RESET)
        {
            RestoreD3DResources();
        }
        else
        {
            throw;
        }
#endif
    }
}

// Release sample's D3D objects.
void D3D12ExecuteIndirect::ReleaseD3DResources()
{
    m_fence.Reset();
    ResetComPtrArray(&m_renderTargets);
    m_commandQueue.Reset();
    m_swapChain.Reset();
    m_device.Reset();
}

// Tears down D3D resources and reinitializes them.
void D3D12ExecuteIndirect::RestoreD3DResources()
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

void D3D12ExecuteIndirect::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void D3D12ExecuteIndirect::OnKeyDown(UINT8 key)
{
    if (key == VK_SPACE)
    {
        m_enableCulling = !m_enableCulling;
    }
}

// Fill the command list with all the render commands and dependent state.
void D3D12ExecuteIndirect::PopulateCommandLists()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_computeState.Get()));

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    
    // Record the compute commands that will cull triangles and prevent them from being processed by the vertex shader.
    D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavHandle = m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

    {
        m_commandList->SetComputeRootSignature(m_computeRootSignature.Get());

        m_commandList->SetComputeRootDescriptorTable(
            SrvUavTable,
            CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvSrvUavHandle, CbvSrvOffset, m_cbvSrvUavDescriptorSize));

        m_commandList->SetComputeRoot32BitConstants(RootConstants, COMPUTE_CONST_VALUE_SLOTS,
            reinterpret_cast<void*>(&m_csRootConstants), 0);

        // Reset the UAV counter for this frame.
        m_commandList->CopyBufferRegion(m_processedCommandBuffer.Get(), CommandBufferCounterOffset,
            m_processedCommandBufferCounterReset.Get(), 0, sizeof(UINT));

        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &barrier);

        m_commandList->Dispatch(static_cast<UINT>(ceil(TriangleCount / float(ComputeThreadBlockSize))), 1, 1);
    }
 
    // Record the rendering commands.
    {
        // Set necessary state.
        m_commandList->SetPipelineState(m_pipelineState.Get());
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        m_commandList->SetGraphicsRoot32BitConstants(ViewData, GRAPHICS_VIEW_DATA_CONST_VALUES,
            &m_viewDataCB, 0);

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, m_enableCulling ? &m_cullingScissorRect : &m_scissorRect);

        // Indicate that the command buffer will be used for indirect drawing
        // and that the back buffer will be used as a render target.
        D3D12_RESOURCE_BARRIER barriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_processedCommandBuffer.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_renderTargets[m_frameIndex].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET)
        };

        m_commandList->ResourceBarrier(_countof(barriers), barriers);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        m_commandList->OMSetRenderTargets(1, &rtvHandle,
            FALSE, &dsvHandle);

        // Record commands.
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        // Draw the triangles that have not been culled.
        m_commandList->ExecuteIndirect(
            m_commandSignature.Get(),
            TriangleCount,
            m_processedCommandBuffer.Get(),
            0,
            m_processedCommandBuffer.Get(),
            CommandBufferCounterOffset);
        
        PIXEndEvent(m_commandList.Get());

        // Indicate that the command buffer may be used by the compute shader
        // and that the back buffer will now be used to present.
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        m_commandList->ResourceBarrier(_countof(barriers), barriers);

        ThrowIfFailed(m_commandList->Close());
    }
}

// Wait for pending GPU work to complete.
void D3D12ExecuteIndirect::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12ExecuteIndirect::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
