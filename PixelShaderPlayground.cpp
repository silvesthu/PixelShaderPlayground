#include <string>
#include <algorithm>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <dxgidebug.h>
#include <d3d12shader.h>
#include <pix3.h>

#include <wrl.h>
using namespace Microsoft::WRL;

#undef min
#undef max

extern "C" { __declspec(dllexport) extern const UINT			D3D12SDKVersion = 614; }
extern "C" { __declspec(dllexport) extern const char8_t*		D3D12SDKPath = u8".\\D3D12\\"; }

static const wchar_t* kApplicationTitleW		= L"Pixel Shader Playground";
static const uint32_t kFrameCount				= 16;
static const uint32_t kTargetSizeX				= 4096;
static const uint32_t kTargetSizeY				= 4096;
static const bool kPrintDisassembly				= false;

static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}

	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

int Run(HWND hwnd)
{
	ComPtr<ID3D12Debug> d3d12_debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12_debug));
	if (GetModuleHandleA("Nvda.Graphics.Interception.dll") == NULL)
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

	ComPtr<IDxcBlob> vs_blob;
	ComPtr<IDxcBlob> ps_blob_a;
	ComPtr<IDxcBlob> ps_blob_b;
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

		auto check_result = [](ComPtr<IDxcOperationResult> result)
		{
			HRESULT compile_result;
			result->GetStatus(&compile_result);

			ComPtr<IDxcBlobEncoding> error_blob;
			if (SUCCEEDED(result->GetErrorBuffer(&error_blob)) && error_blob != nullptr)
			{
				std::string message((const char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
				printf("%s", message.c_str());
				printf("\n");
			}
			return SUCCEEDED(compile_result);
		};

		auto print_reflection = [&](ComPtr<IDxcBlob> blob)
		{
			DxcBuffer dxc_buffer{ .Ptr = blob->GetBufferPointer(), .Size = blob->GetBufferSize(), .Encoding = DXC_CP_ACP };
			ComPtr<ID3D12ShaderReflection> shader_reflection;
			utils->CreateReflection(&dxc_buffer, IID_PPV_ARGS(&shader_reflection));
			UINT64 shader_required_flags = shader_reflection->GetRequiresFlags();
			printf("D3D_SHADER_REQUIRES_WAVE_OPS = %d\n", (shader_required_flags & D3D_SHADER_REQUIRES_WAVE_OPS) ? 1 : 0);
			printf("D3D_SHADER_REQUIRES_DOUBLES = %d\n", (shader_required_flags & D3D_SHADER_REQUIRES_DOUBLES) ? 1 : 0);
			printf("\n");

			ComPtr<IDxcBlobEncoding> disassemble_blob;
			if (kPrintDisassembly && SUCCEEDED(compiler->Disassemble(blob.Get(), &disassemble_blob)))
			{
				std::string message((const char*)disassemble_blob->GetBufferPointer(), disassemble_blob->GetBufferSize());
				printf("%s", message.c_str());
				printf("\n");
			}
		};

		compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"vs_main", L"vs_6_7", arguments, _countof(arguments), defines, _countof(defines), dxc_include_handler.Get(), &result);
		result->GetResult(&vs_blob);
		if (!check_result(result))
			return 0;
		compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"ps_main_a", L"ps_6_7", arguments, _countof(arguments), defines, _countof(defines), dxc_include_handler.Get(), &result);
		result->GetResult(&ps_blob_a);
		if (!check_result(result))
			return 0;
		print_reflection(ps_blob_a);
		compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"ps_main_b", L"ps_6_7", arguments, _countof(arguments), defines, _countof(defines), dxc_include_handler.Get(), &result);
		result->GetResult(&ps_blob_b);
		if (!check_result(result))
			return 0;
		print_reflection(ps_blob_b);
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

	ComPtr<ID3D12Resource> srv_resource;
	D3D12_RESOURCE_DESC srv_resource_desc = rtv.mDesc;
	device->CreateCommittedResource(&rtv.mProperties, D3D12_HEAP_FLAG_NONE, &srv_resource_desc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&srv_resource));
	ComPtr<ID3D12DescriptorHeap> srv_heap;
	D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
	srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv_heap_desc.NumDescriptors = 1;
	srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srv_heap_desc.NodeMask = 1;
	device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap));
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv_desc.Texture2D.MipLevels = (UINT)-1;
	srv_desc.Texture2D.MostDetailedMip = 0;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	device->CreateShaderResourceView(nullptr, &srv_desc, srv_heap->GetCPUDescriptorHandleForHeapStart());

	ComPtr<ID3D12RootSignature> root_signature;
	device->CreateRootSignature(0, ps_blob_a->GetBufferPointer(), ps_blob_a->GetBufferSize(), IID_PPV_ARGS(&root_signature));

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
	pipeline_state_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
	pipeline_state_desc.PS.pShaderBytecode = ps_blob_a->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob_a->GetBufferSize();
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
	ComPtr<ID3D12PipelineState> pso_a;
	device->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&pso_a));
	pipeline_state_desc.PS.pShaderBytecode = ps_blob_b->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob_b->GetBufferSize();
	ComPtr<ID3D12PipelineState> pso_b;
	device->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&pso_b));

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12CommandQueue> command_queue;
	device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

	ComPtr<ID3D12CommandAllocator> command_allocator;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));

	ComPtr<ID3D12GraphicsCommandList> command_list;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list));
	command_list->Close();

	ComPtr<ID3D12Fence> fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	HANDLE event_handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	//////////////////////////////////////////////////////////////////////////

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	ZeroMemory(&swap_chain_desc, sizeof(swap_chain_desc));
	swap_chain_desc.BufferCount = 2;
	swap_chain_desc.Width = 0;
	swap_chain_desc.Height = 0;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
	swap_chain_desc.Stereo = FALSE;
	ComPtr<IDXGISwapChain1> swap_chain_1 = nullptr;
	ComPtr<IDXGISwapChain3> swap_chain = nullptr;
	factory->CreateSwapChainForHwnd(command_queue.Get(), hwnd, &swap_chain_desc, nullptr, nullptr, &swap_chain_1);
	swap_chain_1->QueryInterface(IID_PPV_ARGS(&swap_chain));

	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	//////////////////////////////////////////////////////////////////////////

	uint32_t frame_index = 0;
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		command_allocator->Reset();
		command_list->Reset(command_allocator.Get(), nullptr);

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
		command_list->SetDescriptorHeaps(1, srv_heap.GetAddressOf());
		command_list->RSSetScissorRects(1, &rect);
		command_list->OMSetRenderTargets(1, &rtv_handle, false, nullptr);
		command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		command_list->SetGraphicsRootSignature(root_signature.Get());

		{
			PIXScopedEvent(command_list.Get(), PIX_COLOR(255, 0, 0), "ps_main_a 0");
			command_list->SetPipelineState(pso_a.Get());
			command_list->DrawInstanced(3, 1, 0, 0);
		}
		{
			PIXScopedEvent(command_list.Get(), PIX_COLOR(0, 255, 0), "ps_main_b 0");
			command_list->SetPipelineState(pso_b.Get());
			command_list->DrawInstanced(3, 1, 0, 0);
		}
		{
			PIXScopedEvent(command_list.Get(), PIX_COLOR(255, 0, 0), "ps_main_a 1");
			command_list->SetPipelineState(pso_a.Get());
			command_list->DrawInstanced(3, 1, 0, 0);
		}
		{
			PIXScopedEvent(command_list.Get(), PIX_COLOR(0, 255, 0), "ps_main_b 1");
			command_list->SetPipelineState(pso_b.Get());
			command_list->DrawInstanced(3, 1, 0, 0);
		}

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = rtv.mGPUResource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		command_list->ResourceBarrier(1, &barrier);

		D3D12_TEXTURE_COPY_LOCATION copy_location_src = {};
		copy_location_src.pResource = rtv.mGPUResource.Get();
		copy_location_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		copy_location_src.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION copy_location_dst = {};
		copy_location_dst.pResource = rtv.mReadbackResource.Get();
		copy_location_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copy_location_dst.PlacedFootprint = rtv.mReadbackLayout;
		command_list->CopyTextureRegion(&copy_location_dst, 0, 0, 0, &copy_location_src, nullptr);

		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = rtv.mGPUResource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		command_list->ResourceBarrier(1, &barrier);

		command_list->Close();

		ID3D12CommandList* command_lists[] = { command_list.Get() };
		command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

		swap_chain->Present(1, 0);

		command_queue->Signal(fence.Get(), frame_index);
		fence->SetEventOnCompletion(frame_index, event_handle);
		WaitForSingleObject(event_handle, INFINITE);

		if (frame_index == 0)
		{
			uint8_t* data = nullptr;
			rtv.mReadbackResource->Map(0, nullptr, (void**)&data);

			int pitch = rtv.mReadbackLayout.Footprint.RowPitch;
			for (uint32_t y = 0; y < std::min(kTargetSizeY, 8u); y++)
			{
				uint8_t* row = (data + pitch * y);
				for (uint32_t x = 0; x < std::min(kTargetSizeX, 8u); x++)
				{
					float* float4 = (float*)(row + x * sizeof(float) * 4);
					printf("rtv[%d, %d] = %.3f, %.3f, %.3f, %.3f\n", x, y, float4[0], float4[1], float4[2], float4[3]);
				}
			}

			rtv.mReadbackResource->Unmap(0, nullptr);
		}

		frame_index++;
	}

	return 0;
}

int WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	::AllocConsole();
	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, sWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr};
	::RegisterClassEx(&wc);

	RECT rect = { 0, 0, 400, 400 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, 100, 100, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);

	Run(hwnd);

	::DestroyWindow(hwnd);
	::UnregisterClass(wc.lpszClassName, wc.hInstance);

	::FreeConsole();

	ComPtr<IDXGIDebug1> dxgi_debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
		dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));

	return 0;
}