#include "pch.h"

#include "graphics_engine_state.h"

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

		void submit_one(ID3D12CommandQueue& queue, ID3D12CommandList& command_list)
		{
			const auto list_pointer = &command_list;
			queue.ExecuteCommandLists(1, &list_pointer);
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
	m_frame_resources {create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain)},
	m_fence_current_value {1},
	m_fence {create_fence(*m_device, m_fence_current_value)}
{
}

GSL_SUPPRESS(f .6)
matrix::graphics_engine_state::~graphics_engine_state() noexcept
{
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

void matrix::graphics_engine_state::update()
{
	const auto& [view_handle, buffer, allocator, commands] = wait_for_backbuffer();

	winrt::check_hresult(allocator->Reset());
	winrt::check_hresult(commands->Reset(allocator.get(), nullptr));

	const auto render_state_barrier
		= create_transition_barrier(*buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

	commands->ResourceBarrier(1, &render_state_barrier);

	std::array<float, 4> background_color {0.098f, 0.098f, 0.439f, 1.0f};
	commands->ClearRenderTargetView(view_handle, background_color.data(), 0, nullptr);

	const auto presentation_state_barrier
		= create_transition_barrier(*buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

	commands->ResourceBarrier(1, &presentation_state_barrier);

	winrt::check_hresult(commands->Close());

	submit_one(*m_queue, *commands);
	present(*m_swap_chain);
	signal_frame_submission();
}

void matrix::graphics_engine_state::signal_size_change()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();

	m_frame_resources = {};
	resize(*m_swap_chain);
	m_frame_resources = create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain);
}

void matrix::graphics_engine_state::wait_for_idle()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

const matrix::per_frame_resources& matrix::graphics_engine_state::wait_for_backbuffer()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value - 1)
		_mm_pause();

	return m_frame_resources.at(m_swap_chain->GetCurrentBackBufferIndex());
}

void matrix::graphics_engine_state::signal_frame_submission()
{
	winrt::check_hresult(m_queue->Signal(m_fence.get(), ++m_fence_current_value));
}
