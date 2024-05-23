#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>

#include <wrl.h>
using namespace Microsoft::WRL;

extern "C" { __declspec(dllexport) extern const UINT			D3D12SDKVersion = 614; }
extern "C" { __declspec(dllexport) extern const char8_t*		D3D12SDKPath = u8".\\D3D12\\"; }

int main()
{
	const uint32_t kTargetSizeX					= 8;
	const uint32_t kTargetSizeY					= 4;

	const bool kPrintDisassembly				= false;

	//////////////////////////////////////////////////////////////////////////

	ComPtr<ID3D12Debug> d3d12_debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12_debug));
	d3d12_debug->EnableDebugLayer();

	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

	ComPtr<IDXGIAdapter1> adapter;
	factory->EnumAdapters1(0, &adapter);

	DXGI_ADAPTER_DESC1 adapter_desc;
	adapter->GetDesc1(&adapter_desc);

	ComPtr<ID3D12Device2> device;
	D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device));

	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
	device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1));

	//////////////////////////////////////////////////////////////////////////

	ComPtr<IDxcBlob> vs_blob;
	ComPtr<IDxcBlob> ps_blob;
	{
		ComPtr<IDxcLibrary> library;
		DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));

		ComPtr<IDxcCompiler> compiler;
		DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

		ComPtr<IDxcUtils> utils;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));

		uint32_t code_page = CP_UTF8;
		ComPtr<IDxcBlobEncoding> source_blob;
		library->CreateBlobFromFile(L"Shader.hlsl", &code_page, &source_blob);

		ComPtr<IDxcOperationResult> result;
		LPCWSTR arguments[] =
		{
			L"-O3",
			L"-HV 2021",
			// L"-Zi",
		};

		std::wstring target_size_x_str			= std::to_wstring(kTargetSizeX);
		std::wstring target_size_y_str			= std::to_wstring(kTargetSizeY);
		std::wstring wave_lane_count_min		= std::to_wstring(options1.WaveLaneCountMin);
		std::wstring wave_lane_count_max		= std::to_wstring(options1.WaveLaneCountMax);
		std::wstring total_lane_count			= std::to_wstring(options1.TotalLaneCount);
		DxcDefine defines[] =
		{
			{ L"TARGET_SIZE_X",					target_size_x_str.c_str() },
			{ L"TARGET_SIZE_Y",					target_size_y_str.c_str() },
			{ L"WAVE_LANE_COUNT_MIN",			wave_lane_count_min.c_str() },
			{ L"WAVE_LANE_COUNT_MAX",			wave_lane_count_max.c_str() },
			{ L"TOTAL_LANE_COUNT",				total_lane_count.c_str() },
		};
		for (auto&& define : defines)
			printf("%ls = %ls\n", define.Name, define.Value);
		printf("\n");
		ComPtr<IDxcIncludeHandler> dxc_include_handler;
		utils->CreateDefaultIncludeHandler(&dxc_include_handler);
		if (FAILED(compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"vs_main", L"vs_6_7", arguments, _countof(arguments), defines, _countof(defines), dxc_include_handler.Get(), &result)) ||
			FAILED(result->GetResult(&vs_blob)))
		{
			printf("Vertex shader compile failed\n");
			return 0;
		}
		HRESULT hr = compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"ps_main", L"ps_6_7", arguments, _countof(arguments), defines, _countof(defines), dxc_include_handler.Get(), &result);
		if (SUCCEEDED(hr))
			result->GetStatus(&hr);
		bool compile_succeed = SUCCEEDED(hr);
		ComPtr<IDxcBlobEncoding> error_blob;
		if (SUCCEEDED(result->GetErrorBuffer(&error_blob)) && error_blob)
		{
			printf("Pixel shader compile %s\n", compile_succeed ? "succeed" : "failed");
			std::string message((const char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
			printf("%s", message.c_str());
			printf("\n");
			if (!compile_succeed)
				return 0;
		}

		result->GetResult(&ps_blob);

		DxcBuffer dxc_buffer { .Ptr = ps_blob->GetBufferPointer(), .Size = ps_blob->GetBufferSize(), .Encoding = DXC_CP_ACP };
		ComPtr<ID3D12ShaderReflection> shader_reflection;
		utils->CreateReflection(&dxc_buffer, IID_PPV_ARGS(&shader_reflection));
		UINT64 shader_required_flags = shader_reflection->GetRequiresFlags();
		printf("D3D_SHADER_REQUIRES_WAVE_OPS = %d\n",			(shader_required_flags & D3D_SHADER_REQUIRES_WAVE_OPS) ? 1 : 0);
		printf("D3D_SHADER_REQUIRES_DOUBLES = %d\n",			(shader_required_flags & D3D_SHADER_REQUIRES_DOUBLES) ? 1 : 0);
		printf("\n");

		ComPtr<IDxcBlobEncoding> disassemble_blob;
		if (kPrintDisassembly && SUCCEEDED(compiler->Disassemble(ps_blob.Get(), &disassemble_blob)))
		{
			std::string message((const char*)disassemble_blob->GetBufferPointer(), disassemble_blob->GetBufferSize());
			printf("%s", message.c_str());
			printf("\n");
		}
	}

	//////////////////////////////////////////////////////////////////////////

	struct RTV
	{
		RTV()
		{
			mProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			mProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			mProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			mProperties.CreationNodeMask = 0;
			mProperties.VisibleNodeMask = 0;

			mDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			mDesc.Alignment = 0;
			mDesc.Width = 1;
			mDesc.Height = 1;
			mDesc.DepthOrArraySize = 1;
			mDesc.MipLevels = 1;
			mDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			mDesc.SampleDesc.Count = 1;
			mDesc.SampleDesc.Quality = 0;
			mDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			mDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

			mReadbackProperties = mProperties;
			mReadbackProperties.Type = D3D12_HEAP_TYPE_READBACK;

			mReadbackDesc = mDesc;
			mReadbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			mReadbackDesc.Format = DXGI_FORMAT_UNKNOWN;
			mReadbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			mReadbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		}

		D3D12_RESOURCE_DESC mDesc = {};		
		D3D12_HEAP_PROPERTIES mProperties = {};
		ComPtr<ID3D12Resource> mGPUResource;

		D3D12_RESOURCE_DESC mReadbackDesc = {};
		D3D12_HEAP_PROPERTIES mReadbackProperties = {};
		ComPtr<ID3D12Resource> mReadbackResource;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT mReadbackLayout;
		UINT64 mReadBackBytes;
	};

	RTV rtv;
	rtv.mDesc.Width = kTargetSizeX;
	rtv.mDesc.Height = kTargetSizeY;
	device->CreateCommittedResource(&rtv.mProperties, D3D12_HEAP_FLAG_NONE, &rtv.mDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&rtv.mGPUResource));
	device->GetCopyableFootprints(&rtv.mDesc, 0, 1, 0, &rtv.mReadbackLayout, nullptr, nullptr, &rtv.mReadBackBytes);
	rtv.mReadbackDesc.Width = rtv.mReadBackBytes;
	device->CreateCommittedResource(&rtv.mReadbackProperties, D3D12_HEAP_FLAG_NONE, &rtv.mReadbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rtv.mReadbackResource));

	ComPtr<ID3D12DescriptorHeap> rtv_heap;
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.NumDescriptors = 1;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.NodeMask = 1;
	device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(rtv.mGPUResource.Get(), nullptr, rtv_handle);
	
	ComPtr<ID3D12RootSignature> root_signature;
	device->CreateRootSignature(0, ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
	pipeline_state_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
	pipeline_state_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = root_signature.Get();
	pipeline_state_desc.RasterizerState = rasterizer_desc;
	pipeline_state_desc.BlendState = blend_desc;
	pipeline_state_desc.DepthStencilState.DepthEnable = FALSE;
	pipeline_state_desc.DepthStencilState.StencilEnable = FALSE;
	pipeline_state_desc.SampleMask = UINT_MAX;
	pipeline_state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_state_desc.NumRenderTargets = 1;
	pipeline_state_desc.RTVFormats[0] = rtv.mDesc.Format;
	pipeline_state_desc.SampleDesc.Count = 1;
	ComPtr<ID3D12PipelineState> pso;
	device->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&pso));

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12CommandQueue> command_queue;
	device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

	ComPtr<ID3D12CommandAllocator> command_allocator;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));

	ComPtr<ID3D12GraphicsCommandList> command_list;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list));

	//////////////////////////////////////////////////////////////////////////

	D3D12_RESOURCE_DESC desc = rtv.mDesc;
	D3D12_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(desc.Width);
	viewport.Height = static_cast<float>(desc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	command_list->RSSetViewports(1, &viewport);
	D3D12_RECT rect = {};
	rect.left = 0;
	rect.top = 0;
	rect.right = static_cast<LONG>(desc.Width);
	rect.bottom = static_cast<LONG>(desc.Height);
	command_list->RSSetScissorRects(1, &rect);
	command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	command_list->SetGraphicsRootSignature(root_signature.Get());
	command_list->SetPipelineState(pso.Get());
	command_list->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER barriers[1] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = rtv.mGPUResource.Get();
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	command_list->ResourceBarrier(_countof(barriers), &barriers[0]);	

	D3D12_TEXTURE_COPY_LOCATION copy_location_src = {};
	copy_location_src.pResource = rtv.mGPUResource.Get();
	copy_location_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	copy_location_src.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION copy_location_dst = {};
	copy_location_dst.pResource = rtv.mReadbackResource.Get();
	copy_location_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	copy_location_dst.PlacedFootprint = rtv.mReadbackLayout;
	command_list->CopyTextureRegion(&copy_location_dst, 0, 0, 0, &copy_location_src, nullptr);

	command_list->Close();

	ID3D12CommandList* command_lists[] = { command_list.Get() };
	command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	ComPtr<ID3D12Fence> fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	command_queue->Signal(fence.Get(), 1);

	//////////////////////////////////////////////////////////////////////////
	
	HANDLE handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	fence->SetEventOnCompletion(1, handle);
	WaitForSingleObject(handle, INFINITE);

	uint8_t* data = nullptr;
	rtv.mReadbackResource->Map(0, nullptr, (void**)&data);

	int pitch = rtv.mReadbackLayout.Footprint.RowPitch;
	for (int y = 0; y < kTargetSizeY; y++)
	{
		uint8_t* row = (data + pitch * y);
		for (int x = 0; x < kTargetSizeX; x++)
		{
			float* float4 = (float*)(row + x * sizeof(float) * 4);
			printf("rtv[%d, %d] = %.3f, %.3f, %.3f, %.3f\n", x, y, float4[0], float4[1], float4[2], float4[3]);
		}
	}

	rtv.mReadbackResource->Unmap(0, nullptr);

	return 0;
}