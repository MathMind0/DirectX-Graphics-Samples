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
#include "D3D12HelloComputeShader.h"

#if defined(USE_NSIGHT_AFTERMATH)
#include "NsightAftermathHelpers.h"
#endif

D3D12HelloComputeShader::D3D12HelloComputeShader(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0),
    m_constantBufferBlocks{},
	m_constantBufferBlocksData{},
    m_descriptorSize(0),
    m_fenceEvent(0),
    m_fenceValue(0),
    m_needInit(true),
    m_vertexBufferPosView{},
	m_vertexBufferColorView{}
#if defined(USE_NSIGHT_AFTERMATH)
    , m_hAftermathCommandListContext(nullptr)
    , m_gpuCrashTracker(m_markerMap)
    , m_frameCounter(0)
#endif
{
}

void D3D12HelloComputeShader::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloComputeShader::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG) && !defined(USE_NSIGHT_AFTERMATH)
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
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

#if defined(USE_NSIGHT_AFTERMATH)
        // Enable Nsight Aftermath GPU crash dump creation.
        // This needs to be done before the D3D device is created.
        m_gpuCrashTracker.Initialize();
#endif
        
        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    	NAME_D3D12_OBJECT(m_device);

        #if defined(USE_NSIGHT_AFTERMATH)
        // Initialize Nsight Aftermath for this device.
        //
        // * EnableMarkers - this will include information about the Aftermath
        //   event marker nearest to the crash.
        //
        //   Using event markers should be considered carefully as they can
        //   cause considerable CPU overhead when used in high frequency code
        //   paths. Therefore, on R495 to R530 drivers, the event marker feature is
        //   only available if the Nsight Aftermath GPU Crash Dump Monitor is running
        //   on the system. No Aftermath configuration needs to be made in the
        //   Monitor. It serves only as a dongle to ensure Aftermath event
        //   markers do not impact application performance on end user systems.
        //
        // * EnableResourceTracking - this will include additional information about the
        //   resource related to a GPU virtual address seen in case of a crash due to a GPU
        //   page fault. This includes, for example, information about the size of the
        //   resource, its format, and an indication if the resource has been deleted.
        //
        // * CallStackCapturing - this will include call stack and module information for
        //   the draw call, compute dispatch, or resource copy nearest to the crash.
        //
        //   Enabling this feature will cause very high CPU overhead during command list
        //   recording. Due to the inherent overhead, call stack capturing should only
        //   be used for debugging purposes on development or QA systems and should not be
        //   enabled in applications shipped to customers. Therefore, on R495+ drivers,
        //   the call stack capturing feature is only available if the Nsight Aftermath GPU
        //   Crash Dump Monitor is running on the system. No Aftermath configuration needs
        //   to be made in the Monitor. It serves only as a dongle to ensure Aftermath call
        //   stack capturing does not impact application performance on end user systems.
        //
        // * GenerateShaderDebugInfo - this instructs the shader compiler to
        //   generate debug information (line tables) for all shaders. Using this option
        //   should be considered carefully. It may cause considerable shader compilation
        //   overhead and additional overhead for handling the corresponding shader debug
        //   information callbacks.
        //
        const uint32_t aftermathFlags =
            GFSDK_Aftermath_FeatureFlags_EnableMarkers |             // Enable event marker tracking.
            GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
            GFSDK_Aftermath_FeatureFlags_CallStackCapturing |        // Capture call stacks for all draw calls, compute dispatches, and resource copies.
            GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo;    // Generate debug information for shaders.

        AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_Initialize(
            GFSDK_Aftermath_Version_API,
            aftermathFlags,
            m_device.Get()));
#endif
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
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    	NAME_D3D12_OBJECT(m_rtvHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Describe and create a constant buffer view (CBV) descriptor heap.
        // Flags indicate that this descriptor heap can be bound to the pipeline 
        // and that descriptors contained in it can be referenced by a root table.
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = DESCRIPTOR_NUM;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_heapDescriptors)));
    	NAME_D3D12_OBJECT(m_heapDescriptors);
    	
        m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
	NAME_D3D12_OBJECT(m_commandAllocator);
	
	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
	NAME_D3D12_OBJECT(m_commandList);

#if defined(USE_NSIGHT_AFTERMATH)
	// Create an Nsight Aftermath context handle for setting Aftermath event markers in this command list.
	AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_CreateContextHandle(m_commandList.Get(), &m_hAftermathCommandListContext));
#endif
    
	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());
}

// Load the sample assets.
void D3D12HelloComputeShader::LoadAssets()
{
	// Create a root signature consisting of a descriptor table with a single CBV.
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		{
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamPos[1];
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamColor[1];
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamVelocity[1];
			CD3DX12_ROOT_PARAMETER1 rootParameters[INIT_BLOCKS_PARAM_NUM];

			rangesParamPos[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			rootParameters[INIT_BLOCKS_POS_UAV].InitAsDescriptorTable(1, rangesParamPos, D3D12_SHADER_VISIBILITY_ALL);
			
			rangesParamColor[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			rootParameters[INIT_BLOCKS_COLOR_UAV].InitAsDescriptorTable(1, rangesParamColor, D3D12_SHADER_VISIBILITY_ALL);

			rangesParamVelocity[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			rootParameters[INIT_BLOCKS_VELOCITY_UAV].InitAsDescriptorTable(1, rangesParamVelocity, D3D12_SHADER_VISIBILITY_ALL);
			
            rootParameters[INIT_BLOCKS_TILE_CB].InitAsConstantBufferView(0, 0,
                D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);


			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
				signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureInitBlocks)));
			NAME_D3D12_OBJECT(m_rootSignatureInitBlocks);
		}

		{
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamPosUAV[1];
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamPosSRV[1];
			CD3DX12_DESCRIPTOR_RANGE1 rangesParamVelocityUAV[1];
			CD3DX12_ROOT_PARAMETER1 rootParameters[UPDATE_BLOCKS_PARAM_NUM];

			rangesParamPosUAV[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			rootParameters[UPDATE_BLOCKS_POS_UAV].InitAsDescriptorTable(1, rangesParamPosUAV, D3D12_SHADER_VISIBILITY_ALL);
			
			rangesParamPosSRV[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[UPDATE_BLOCKS_POS_SRV].InitAsDescriptorTable(1, rangesParamPosSRV, D3D12_SHADER_VISIBILITY_ALL);

			rangesParamVelocityUAV[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0,
				D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			rootParameters[UPDATE_BLOCKS_VELOCITY_UAV].InitAsDescriptorTable(1, rangesParamVelocityUAV, D3D12_SHADER_VISIBILITY_ALL);
			
			rootParameters[UPDATE_BLOCKS_TILE_CB].InitAsConstantBufferView(0, 0,
				D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);


			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
				signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureUpdateBlocks)));
			NAME_D3D12_OBJECT(m_rootSignatureUpdateBlocks);
		}

		// Create an empty root signature.
		{
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
				signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureDraw)));
			NAME_D3D12_OBJECT(m_rootSignatureDraw);
		}
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> computeShaderInitBlocks;
		ComPtr<ID3DBlob> computeShaderUpdateBlocks;
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
			"CSInitBlocks", "cs_5_0", compileFlags, 0, &computeShaderInitBlocks, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
			"CSUpdateBlocks", "cs_5_0", compileFlags, 0, &computeShaderUpdateBlocks, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
			"VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr,
			"PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Describe and create the compute pipeline state object (PSO).
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
			computePsoDesc.pRootSignature = m_rootSignatureInitBlocks.Get();
			computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderInitBlocks.Get());

			ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_pipelineStateInitBlocks)));
			NAME_D3D12_OBJECT(m_pipelineStateInitBlocks);
		}

		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
			computePsoDesc.pRootSignature = m_rootSignatureUpdateBlocks.Get();
			computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderUpdateBlocks.Get());

			ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_pipelineStateUpdateBlocks)));
			NAME_D3D12_OBJECT(m_pipelineStateUpdateBlocks);
		}

		{
			// Define the vertex input layout.
			D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
					D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,
					D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// Describe and create the graphics pipeline state object (PSO).
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
			psoDesc.pRootSignature = m_rootSignatureDraw.Get();
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState.DepthEnable = FALSE;
			psoDesc.DepthStencilState.StencilEnable = FALSE;
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.SampleDesc.Count = 1;

			ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateDraw)));
			NAME_D3D12_OBJECT(m_pipelineStateDraw);
		}
	}
    
    // Create the vertex buffer.
	UINT numBlocks = 64 * TILE_NUM * TILE_NUM;
	UINT numVertices = numBlocks * 6;
	UINT szBufferPos = numVertices * sizeof(float) * 3;
	UINT szBufferColor = numVertices * sizeof(float) * 3;
	UINT szBufferVelocity = numBlocks * sizeof(float) * 2;
	
    {
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = numVertices;
		uavDesc.Buffer.StructureByteStride = sizeof(float) * 3;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = numVertices;
		srvDesc.Buffer.StructureByteStride = sizeof(float) * 3;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				
		for (int i = 0; i < 2; ++i)
		{
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(szBufferPos, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&m_vertexBufferPos[i])));

			NAME_D3D12_OBJECT(m_vertexBufferPos[i]);
		
			// Initialize the vertex buffer view.
			m_vertexBufferPosView[i].BufferLocation = m_vertexBufferPos[i]->GetGPUVirtualAddress();
			m_vertexBufferPosView[i].StrideInBytes = sizeof(float) * 3;
			m_vertexBufferPosView[i].SizeInBytes = szBufferPos;

			CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_heapDescriptors->GetCPUDescriptorHandleForHeapStart(),
				i == 0 ? OFFSET_VERTEX_BUFFER_POS_UAV0 : OFFSET_VERTEX_BUFFER_POS_UAV1, m_descriptorSize);
			m_device->CreateUnorderedAccessView(m_vertexBufferPos[i].Get(), nullptr, &uavDesc, uavHandle);

			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_heapDescriptors->GetCPUDescriptorHandleForHeapStart(),
				i == 0 ? OFFSET_VERTEX_BUFFER_POS_SRV0 : OFFSET_VERTEX_BUFFER_POS_SRV1, m_descriptorSize);
			m_device->CreateShaderResourceView(m_vertexBufferPos[i].Get(), &srvDesc, srvHandle);
		}
		
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(szBufferColor, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&m_vertexBufferColor)));

		NAME_D3D12_OBJECT(m_vertexBufferColor);

		// Initialize the vertex buffer view.
		m_vertexBufferColorView.BufferLocation = m_vertexBufferColor->GetGPUVirtualAddress();
		m_vertexBufferColorView.StrideInBytes = sizeof(float) * 3;
		m_vertexBufferColorView.SizeInBytes = szBufferColor;		

		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_heapDescriptors->GetCPUDescriptorHandleForHeapStart(),
			OFFSET_VERTEX_BUFFER_COLOR_UAV, m_descriptorSize);
		m_device->CreateUnorderedAccessView(m_vertexBufferColor.Get(), nullptr, &uavDesc, uavHandle);

        m_needInit = true;
    }

    // Create the velocity buffer.
    {	    
	    ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(szBufferVelocity, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_velocityBuffer)));

		NAME_D3D12_OBJECT(m_velocityBuffer);

	    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	    uavDesc.Buffer.FirstElement = 0;
	    uavDesc.Buffer.NumElements = numBlocks;
	    uavDesc.Buffer.StructureByteStride = sizeof(float) * 2;
	    uavDesc.Buffer.CounterOffsetInBytes = 0;
	    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_heapDescriptors->GetCPUDescriptorHandleForHeapStart(),
            OFFSET_VELOCITY_BUFFER_UAV, m_descriptorSize);
	    m_device->CreateUnorderedAccessView(m_velocityBuffer.Get(), nullptr, &uavDesc, uavHandle);
    }
	
    // Create the constant buffer.
    {
        const UINT constantBufferSize = sizeof(BlocksConstantBuffer);    // CB size is required to be 256-byte aligned.

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBufferBlocks)));

        NAME_D3D12_OBJECT(m_constantBufferBlocks);
        
        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        m_constantBufferBlocksData.nTiles.x = TILE_NUM;
        m_constantBufferBlocksData.blockWidth.x = 2.f / (8 * TILE_NUM);

        UINT8* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBufferBlocks->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, &m_constantBufferBlocksData, sizeof(m_constantBufferBlocksData));
        m_constantBufferBlocks->Unmap(0, nullptr);
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

// Update frame-based values.
void D3D12HelloComputeShader::OnUpdate()
{
}

// Render the scene.
void D3D12HelloComputeShader::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
#if defined(USE_NSIGHT_AFTERMATH)
    HRESULT hr = m_swapChain->Present(1, 0);
    if (FAILED(hr))
    {
        // DXGI_ERROR error notification is asynchronous to the NVIDIA display
        // driver's GPU crash handling. Give the Nsight Aftermath GPU crash dump
        // thread some time to do its work before terminating the process.
        auto tdrTerminationTimeout = std::chrono::seconds(3);
        auto tStart = std::chrono::steady_clock::now();
        auto tElapsed = std::chrono::milliseconds::zero();

        GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
        AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetCrashDumpStatus(&status));

        while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&
               status != GFSDK_Aftermath_CrashDump_Status_Finished &&
               tElapsed < tdrTerminationTimeout)
        {
            // Sleep 50ms and poll the status again until timeout or Aftermath finished processing the crash dump.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GetCrashDumpStatus(&status));

            auto tEnd = std::chrono::steady_clock::now();
            tElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
        }

        if (status != GFSDK_Aftermath_CrashDump_Status_Finished)
        {
            std::stringstream err_msg;
            err_msg << "Unexpected crash dump status: " << status;
            MessageBoxA(NULL, err_msg.str().c_str(), "Aftermath Error", MB_OK);
        }

        // Terminate on failure
        exit(-1);
    }
    m_frameCounter++;
#else
    ThrowIfFailed(m_swapChain->Present(1, 0));
#endif

    WaitForPreviousFrame();
}

void D3D12HelloComputeShader::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

// Fill the command list with all the render commands and dependent state.
void D3D12HelloComputeShader::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    ID3D12DescriptorHeap* ppHeaps[] = { m_heapDescriptors.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	UINT idxVertexBufferUAV = m_frameIndex & 0x01;
	UINT idxVertexBufferSRV = 1 - idxVertexBufferUAV;
	INT offsetVertexBufferUAV = idxVertexBufferUAV ? OFFSET_VERTEX_BUFFER_POS_UAV1 : OFFSET_VERTEX_BUFFER_POS_UAV0;
	INT offsetVertexBufferSRV = idxVertexBufferSRV ? OFFSET_VERTEX_BUFFER_POS_SRV1 : OFFSET_VERTEX_BUFFER_POS_SRV0;
	
    if (m_needInit)
    {
    	CD3DX12_RESOURCE_BARRIER barriersBefore[] = {
    		CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBufferPos[idxVertexBufferUAV].Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				)
		};
    	m_commandList->ResourceBarrier(_countof(barriersBefore), barriersBefore);
    	
        m_commandList->SetComputeRootSignature(m_rootSignatureInitBlocks.Get());
    	m_commandList->SetPipelineState(m_pipelineStateInitBlocks.Get());
        
        CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVertexBufferPos(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
            offsetVertexBufferUAV, m_descriptorSize);
    	CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVertexBufferColor(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
			OFFSET_VERTEX_BUFFER_COLOR_UAV, m_descriptorSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVelocityBuffer(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
            OFFSET_VELOCITY_BUFFER_UAV, m_descriptorSize);
        
        m_commandList->SetComputeRootDescriptorTable(INIT_BLOCKS_POS_UAV, uavHandleVertexBufferPos);
    	m_commandList->SetComputeRootDescriptorTable(INIT_BLOCKS_COLOR_UAV, uavHandleVertexBufferColor);
        m_commandList->SetComputeRootDescriptorTable(INIT_BLOCKS_VELOCITY_UAV, uavHandleVelocityBuffer);
        m_commandList->SetComputeRootConstantBufferView(INIT_BLOCKS_TILE_CB, m_constantBufferBlocks->GetGPUVirtualAddress());

        m_commandList->Dispatch(TILE_NUM, TILE_NUM, 1);

        CD3DX12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBufferPos[idxVertexBufferUAV].Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        	CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBufferColor.Get(),
        		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(m_velocityBuffer.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };
        
        m_commandList->ResourceBarrier(_countof(barriers), barriers);
        m_needInit = false;
    }
	else
	{
		CD3DX12_RESOURCE_BARRIER barriersBefore[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBufferPos[idxVertexBufferUAV].Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				)
		};
        m_commandList->ResourceBarrier(_countof(barriersBefore), barriersBefore);

		m_commandList->SetComputeRootSignature(m_rootSignatureUpdateBlocks.Get());
		m_commandList->SetPipelineState(m_pipelineStateUpdateBlocks.Get());
		        
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVertexBufferPosUAV(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
			offsetVertexBufferUAV, m_descriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVertexBufferPosSRV(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
			offsetVertexBufferSRV, m_descriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandleVelocityBuffer(m_heapDescriptors->GetGPUDescriptorHandleForHeapStart(),
			OFFSET_VELOCITY_BUFFER_UAV, m_descriptorSize);
        
		m_commandList->SetComputeRootDescriptorTable(UPDATE_BLOCKS_POS_UAV, uavHandleVertexBufferPosUAV);
		m_commandList->SetComputeRootDescriptorTable(UPDATE_BLOCKS_POS_SRV, uavHandleVertexBufferPosSRV);
		m_commandList->SetComputeRootDescriptorTable(UPDATE_BLOCKS_VELOCITY_UAV, uavHandleVelocityBuffer);
		m_commandList->SetComputeRootConstantBufferView(UPDATE_BLOCKS_TILE_CB, m_constantBufferBlocks->GetGPUVirtualAddress());

		m_commandList->Dispatch(TILE_NUM, TILE_NUM, 1);

		CD3DX12_RESOURCE_BARRIER barriersAfter[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBufferPos[idxVertexBufferUAV].Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
		};
        
		m_commandList->ResourceBarrier(_countof(barriersAfter), barriersAfter);
	}
    
    // Set necessary state.
	m_commandList->SetPipelineState(m_pipelineStateDraw.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignatureDraw.Get());

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle,
        FALSE, nullptr);
    
#if defined(USE_NSIGHT_AFTERMATH)
    // A helper for setting Aftermath event markers.
    // For maximum CPU performance, use GFSDK_Aftermath_SetEventMarker() with dataSize=0.
    // This instructs Aftermath not to allocate and copy off memory internally, relying on
    // the application to manage marker pointers itself.
    auto setAftermathEventMarker = [this](const std::string& markerData, bool appManagedMarker)
    {
        if (appManagedMarker)
        {
            // App is responsible for handling marker memory, and for resolving the memory at crash dump generation time.
            // The actual "const void* markerData" passed to Aftermath in this case can be any uniquely identifying value that the app can resolve to the marker data later.
            // For this sample, we will use this approach to generating a unique marker value:
            // We keep a ringbuffer with a marker history of the last c_markerFrameHistory frames (currently 4).
            UINT markerMapIndex = m_frameCounter % GpuCrashTracker::c_markerFrameHistory;
            auto& currentFrameMarkerMap = m_markerMap[markerMapIndex];
            // Take the index into the ringbuffer, multiply by 10000, and add the total number of markers logged so far in the current frame, +1 to avoid a value of zero.
            size_t markerID = markerMapIndex * 10000 + currentFrameMarkerMap.size() + 1;
            // This value is the unique identifier we will pass to Aftermath and internally associate with the marker data in the map.
            currentFrameMarkerMap[markerID] = markerData;
            AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_SetEventMarker(m_hAftermathCommandListContext, (void*)markerID, 0));
            // For example, if we are on frame 625, markerMapIndex = 625 % 4 = 1...
            // The first marker for the frame will have markerID = 1 * 10000 + 0 + 1 = 10001.
            // The 15th marker for the frame will have markerID = 1 * 10000 + 14 + 1 = 10015.
            // On the next frame, 626, markerMapIndex = 626 % 4 = 2.
            // The first marker for this frame will have markerID = 2 * 10000 + 0 + 1 = 20001.
            // The 15th marker for the frame will have markerID = 2 * 10000 + 14 + 1 = 20015.
            // So with this scheme, we can safely have up to 10000 markers per frame, and can guarantee a unique markerID for each one.
            // There are many ways to generate and track markers and unique marker identifiers!
        }
        else
        {
            AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_SetEventMarker(m_hAftermathCommandListContext, (void*)markerData.c_str(), (unsigned int)markerData.size() + 1));
        }
    };
    // clear the marker map for the current frame before writing any markers
    m_markerMap[m_frameCounter % GpuCrashTracker::c_markerFrameHistory].clear();

    // A helper that prepends the frame number to a string
    auto createMarkerStringForFrame = [this](const char* markerString) {
        std::stringstream ss;
        ss << "Frame " << m_frameCounter << ": " << markerString;
        return ss.str();
    };
#endif
    
    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };

#if defined(USE_NSIGHT_AFTERMATH)
    // Inject a marker in the command list before clearing the render target.
    // Second argument appManagedMarker=false means that Aftermath will internally copy the marker data
    setAftermathEventMarker(createMarkerStringForFrame("Clear Render Target"), false);
#endif
    
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferPosView[idxVertexBufferUAV]);
	m_commandList->IASetVertexBuffers(1, 1, &m_vertexBufferPosView[idxVertexBufferUAV]);

#if defined(USE_NSIGHT_AFTERMATH)
    // Inject a marker in the command list before the draw call.
    // Second argument appManagedMarker=true means that Aftermath will not copy marker data and depend on the app to resolve the marker later
    setAftermathEventMarker(createMarkerStringForFrame("Draw Blocks"), true);
#endif
    
    m_commandList->DrawInstanced(64 * TILE_NUM * TILE_NUM * 6,
        1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloComputeShader::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
