#include "pch.h"

#include "graphics_engine_state.h"

#include "shader_loading.h"

namespace matrix {
	namespace {
		auto create_dxgi_factory()
		{
			return winrt::capture<IDXGIFactory6>(
				CreateDXGIFactory2, IsDebuggerPresent() ? DXGI_CREATE_FACTORY_DEBUG : 0);
		}

		auto create_gpu_device(IDXGIFactory6& dxgi_factory)
		{
			if (IsDebuggerPresent())
				winrt::capture<ID3D12Debug>(D3D12GetDebugInterface)->EnableDebugLayer();

			const auto selected_adapter = winrt::capture<IUnknown>(
				&dxgi_factory, &IDXGIFactory6::EnumAdapterByGpuPreference, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);

			return winrt::capture<ID3D12Device4>(D3D12CreateDevice, selected_adapter.get(), D3D_FEATURE_LEVEL_12_1);
		}

		auto create_command_queue(ID3D12Device& device)
		{
			D3D12_COMMAND_QUEUE_DESC description {};
			return winrt::capture<ID3D12CommandQueue>(&device, &ID3D12Device::CreateCommandQueue, &description);
		}

		auto create_swap_chain(IDXGIFactory3& factory, ID3D12CommandQueue& presenter_queue, HWND target_window)
		{
			DXGI_SWAP_CHAIN_DESC1 description {};
			description.BufferCount = 2;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			winrt::com_ptr<IDXGISwapChain1> swap_chain {};
			winrt::check_hresult(factory.CreateSwapChainForHwnd(
				&presenter_queue, target_window, &description, nullptr, nullptr, swap_chain.put()));

			// We do not currently support exclusive-mode fullscreen
			winrt::check_hresult(factory.MakeWindowAssociation(target_window, DXGI_MWA_NO_ALT_ENTER));

			return swap_chain.as<IDXGISwapChain3>();
		}

		void present(IDXGISwapChain& swap_chain) { winrt::check_hresult(swap_chain.Present(1, 0)); }

		auto create_fence(ID3D12Device& device, std::uint64_t initial_value)
		{
			return winrt::capture<ID3D12Fence>(
				&device, &ID3D12Device::CreateFence, initial_value, D3D12_FENCE_FLAG_NONE);
		}

		void resize(IDXGISwapChain& swap_chain)
		{
			OutputDebugStringW(L"[note] resize triggered\n");
			winrt::check_hresult(swap_chain.ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
		}

		auto
		create_descriptor_heap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE descriptor_type, unsigned int capacity)
		{
			D3D12_DESCRIPTOR_HEAP_DESC description {};
			description.Type = descriptor_type;
			description.NumDescriptors = capacity;
			return winrt::capture<ID3D12DescriptorHeap>(&device, &ID3D12Device::CreateDescriptorHeap, &description);
		}

		std::array<per_frame_resources, 2>
		create_frame_resources(ID3D12Device4& device, ID3D12DescriptorHeap& rtv_heap, IDXGISwapChain& swap_chain)
		{
			const auto handle_size = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_RENDER_TARGET_VIEW_DESC description {};
			description.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto descriptor_handle = rtv_heap.GetCPUDescriptorHandleForHeapStart();
			std::array<per_frame_resources, 2> frame_resources {};
			for (unsigned int i {}; i < 2; ++i, descriptor_handle.ptr += handle_size) {
				const auto backbuffer = winrt::capture<ID3D12Resource>(&swap_chain, &IDXGISwapChain::GetBuffer, i);
				device.CreateRenderTargetView(backbuffer.get(), &description, descriptor_handle);
				frame_resources.at(i)
					= {descriptor_handle,
					   backbuffer,
					   winrt::capture<ID3D12CommandAllocator>(
						   &device, &ID3D12Device::CreateCommandAllocator, D3D12_COMMAND_LIST_TYPE_DIRECT),
					   winrt::capture<ID3D12GraphicsCommandList>(
						   &device,
						   &ID3D12Device4::CreateCommandList1,
						   0,
						   D3D12_COMMAND_LIST_TYPE_DIRECT,
						   D3D12_COMMAND_LIST_FLAG_NONE)};
			}

			return frame_resources;
		}

		D3D12_RESOURCE_BARRIER create_transition_barrier(
			ID3D12Resource& resource, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after) noexcept
		{
			D3D12_RESOURCE_BARRIER transition_barrier {};
			transition_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			transition_barrier.Transition.StateBefore = state_before;
			transition_barrier.Transition.StateAfter = state_after;
			transition_barrier.Transition.pResource = &resource;
			return transition_barrier;
		}

		template <typename... list_types>
		void execute_command_lists(ID3D12CommandQueue& queue, list_types&... command_lists)
		{
			std::array<ID3D12CommandList*, sizeof...(command_lists)> list_pointers {&command_lists...};
			queue.ExecuteCommandLists(gsl::narrow<UINT>(list_pointers.size()), list_pointers.data());
		}

		template <typename... barrier_types>
		void submit_resource_barriers(ID3D12GraphicsCommandList& command_list, barrier_types... barriers)
		{
			const std::array<D3D12_RESOURCE_BARRIER, sizeof...(barrier_types)> all_barriers {barriers...};
			command_list.ResourceBarrier(gsl::narrow<UINT>(all_barriers.size()), all_barriers.data());
		}

		void clear_render_target(
			ID3D12GraphicsCommandList& command_list,
			D3D12_CPU_DESCRIPTOR_HANDLE view_handle,
			float red = 0.0f,
			float green = 0.0f,
			float blue = 0.0f,
			float alpha = 1.0f)
		{
			const std::array color {red, green, blue, alpha};
			command_list.ClearRenderTargetView(view_handle, color.data(), 0, nullptr);
		}

		auto create_default_pipeline_state(ID3D12Device& device, const root_signature_table& root_signatures)
		{
			const auto vertex_shader = load_compiled_shader(L"vertex.cso");
			const auto pixel_shader = load_compiled_shader(L"pixel.cso");

			D3D12_GRAPHICS_PIPELINE_STATE_DESC info {};
			info.pRootSignature = root_signatures.default_signature.get();
			info.VS.BytecodeLength = vertex_shader.size();
			info.VS.pShaderBytecode = vertex_shader.data();
			info.PS.BytecodeLength = pixel_shader.size();
			info.PS.pShaderBytecode = pixel_shader.data();
			info.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
			info.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			info.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			info.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
			info.RasterizerState.DepthClipEnable = true;
			// info.RasterizerState.FrontCounterClockwise = true;
			info.DepthStencilState.DepthEnable = true;
			info.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
			info.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			info.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			info.NumRenderTargets = 1;
			info.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			info.SampleDesc.Count = 1;

			/*D3D12_INPUT_ELEMENT_DESC position {};
			position.Format = DXGI_FORMAT_R32G32B32_FLOAT;
			position.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			position.SemanticName = "POSITION";

			info.InputLayout.NumElements = 1;
			info.InputLayout.pInputElementDescs = &position;*/

			return winrt::capture<ID3D12PipelineState>(&device, &ID3D12Device::CreateGraphicsPipelineState, &info);
		}

		auto create_root_signature(ID3D12Device& device)
		{
			D3D12_ROOT_PARAMETER constants {};
			constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			constants.Constants.Num32BitValues = 4 * 4 * 2;

			D3D12_ROOT_SIGNATURE_DESC info {};
			info.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			info.NumParameters = 1;
			info.pParameters = &constants;

			winrt::com_ptr<ID3DBlob> result {};
			winrt::com_ptr<ID3DBlob> error {};
			winrt::check_hresult(
				D3D12SerializeRootSignature(&info, D3D_ROOT_SIGNATURE_VERSION_1, result.put(), error.put()));

			return winrt::capture<ID3D12RootSignature>(
				&device, &ID3D12Device::CreateRootSignature, 0, result->GetBufferPointer(), result->GetBufferSize());
		}

		void maximize_rasterizer(ID3D12GraphicsCommandList& list, ID3D12Resource& target)
		{
			const auto info = target.GetDesc();

			D3D12_RECT scissor {};
			scissor.right = gsl::narrow_cast<long>(info.Width);
			scissor.bottom = gsl::narrow_cast<long>(info.Height);

			D3D12_VIEWPORT viewport {};
			viewport.Width = gsl::narrow_cast<float>(info.Width);
			viewport.Height = gsl::narrow_cast<float>(info.Height);
			viewport.MaxDepth = 1.0f;

			list.RSSetScissorRects(1, &scissor);
			list.RSSetViewports(1, &viewport);
		}

		struct extent2d {
			UINT width;
			UINT height;
		};

		extent2d get_extent(IDXGISwapChain& swap_chain)
		{
			DXGI_SWAP_CHAIN_DESC description {};
			winrt::check_hresult(swap_chain.GetDesc(&description));
			return {description.BufferDesc.Width, description.BufferDesc.Height};
		}

		auto create_depth_buffer(ID3D12Device& device, D3D12_CPU_DESCRIPTOR_HANDLE dsv, const extent2d& size)
		{
			D3D12_HEAP_PROPERTIES properties {};
			properties.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_DESC info {};
			info.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			info.DepthOrArraySize = 1;
			info.Width = size.width;
			info.Height = size.height;
			info.MipLevels = 1;
			info.SampleDesc.Count = 1;
			info.Format = DXGI_FORMAT_D32_FLOAT;
			info.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE clear_value {};
			clear_value.DepthStencil.Depth = 1.0f;
			clear_value.Format = info.Format;

			const auto buffer = winrt::capture<ID3D12Resource>(
				&device,
				&ID3D12Device::CreateCommittedResource,
				&properties,
				D3D12_HEAP_FLAG_NONE,
				&info,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&clear_value);

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_info {};
			dsv_info.Format = info.Format;
			dsv_info.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			device.CreateDepthStencilView(buffer.get(), &dsv_info, dsv);

			return buffer;
		}

		root_signature_table create_root_signatures(ID3D12Device& device)
		{
			return {.default_signature {create_root_signature(device)}};
		}
	}
}

matrix::graphics_engine_state::graphics_engine_state(HWND target_window) :
	graphics_engine_state {*create_dxgi_factory(), target_window}
{
}

matrix::graphics_engine_state::graphics_engine_state(IDXGIFactory6& factory, HWND target_window) :
	m_device {create_gpu_device(factory)},
	m_queue {create_command_queue(*m_device)},
	m_swap_chain {create_swap_chain(factory, *m_queue, target_window)},
	m_rtv_heap {create_descriptor_heap(*m_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2)},
	m_dsv_heap {create_descriptor_heap(*m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1)},
	m_root_signatures {create_root_signatures(*m_device)},
	m_debug_grid_pass {create_default_pipeline_state(*m_device, m_root_signatures)},
	m_depth_buffer {
		create_depth_buffer(*m_device, m_dsv_heap->GetCPUDescriptorHandleForHeapStart(), get_extent(*m_swap_chain))},
	m_frame_resources {create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain)},
	m_fence_current_value {1},
	m_fence {create_fence(*m_device, m_fence_current_value)}
{
}

GSL_SUPPRESS(f .6)
matrix::graphics_engine_state::~graphics_engine_state() noexcept { wait_for_idle(); }

void matrix::graphics_engine_state::update()
{
	const auto& [view_handle, buffer, allocator, commands] = wait_for_frame();

	winrt::check_hresult(allocator->Reset());
	winrt::check_hresult(commands->Reset(allocator.get(), m_debug_grid_pass.get()));

	const auto extent = get_extent(*m_swap_chain);
	const auto aspect = gsl::narrow<float>(extent.width) / extent.height;
	const auto view = DirectX::XMMatrixRotationZ(m_fence_current_value * 3.14159265f / 480.0f)
		* DirectX::XMMatrixRotationX(3.14159265f / 3.0f) * DirectX::XMMatrixTranslation(0.0f, 0.0f, 1.0f);

	const auto projection = DirectX::XMMatrixPerspectiveFovLH(3.141f / 2.0f, aspect, 0.01f, 10.0f);

	commands->SetGraphicsRootSignature(m_root_signatures.default_signature.get());
	commands->SetGraphicsRoot32BitConstants(0, 16, &view, 0);
	commands->SetGraphicsRoot32BitConstants(0, 16, &projection, 16);

	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	maximize_rasterizer(*commands, *buffer);
	const auto depth_buffer_view = m_dsv_heap->GetCPUDescriptorHandleForHeapStart();
	commands->OMSetRenderTargets(1, &view_handle, false, &depth_buffer_view);

	commands->ClearDepthStencilView(depth_buffer_view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	submit_resource_barriers(
		*commands, create_transition_barrier(*buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

	clear_render_target(*commands, view_handle);
	commands->DrawInstanced(2, 18, 0, 0);
	submit_resource_barriers(
		*commands, create_transition_barrier(*buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));

	winrt::check_hresult(commands->Close());

	execute_command_lists(*m_queue, *commands);
	present(*m_swap_chain);
	signal_frame_submission();
}

void matrix::graphics_engine_state::signal_size_change()
{
	wait_for_idle();
	m_frame_resources = {};
	resize(*m_swap_chain);
	m_frame_resources = create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain);
	m_depth_buffer
		= create_depth_buffer(*m_device, m_dsv_heap->GetCPUDescriptorHandleForHeapStart(), get_extent(*m_swap_chain));
}

void matrix::graphics_engine_state::wait_for_idle()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

const matrix::per_frame_resources& matrix::graphics_engine_state::wait_for_frame()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value - 1)
		_mm_pause();

	return m_frame_resources.at(m_swap_chain->GetCurrentBackBufferIndex());
}

void matrix::graphics_engine_state::signal_frame_submission()
{
	winrt::check_hresult(m_queue->Signal(m_fence.get(), ++m_fence_current_value));
}
