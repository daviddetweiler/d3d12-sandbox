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
		create_frame_resources(ID3D12Device& device, ID3D12DescriptorHeap& rtv_heap, IDXGISwapChain& swap_chain)
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
				frame_resources.at(i) = {descriptor_handle, backbuffer};
			}

			return frame_resources;
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
	m_allocator {winrt::capture<ID3D12CommandAllocator>(
		m_device, &ID3D12Device::CreateCommandAllocator, D3D12_COMMAND_LIST_TYPE_DIRECT)},
	m_command_list {winrt::capture<ID3D12GraphicsCommandList>(
		m_device, &ID3D12Device4::CreateCommandList1, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE)},
	m_frame_resources {create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain)},
	m_fence_current_value {},
	m_fence {create_fence(*m_device, m_fence_current_value)}
{
}

void matrix::graphics_engine_state::update()
{
	const auto current_resources = m_frame_resources.at(m_swap_chain->GetCurrentBackBufferIndex());
	winrt::check_hresult(m_allocator->Reset());
	winrt::check_hresult(m_command_list->Reset(m_allocator.get(), nullptr));

	D3D12_RESOURCE_BARRIER render_state_barrier {};
	render_state_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	render_state_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	render_state_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	render_state_barrier.Transition.pResource = current_resources.swap_chain_buffer.get();
	m_command_list->ResourceBarrier(1, &render_state_barrier);

	std::array<float, 4> background_color {0.098f, 0.098f, 0.439f, 1.0f};
	m_command_list->ClearRenderTargetView(
		current_resources.render_target_view_handle, background_color.data(), 0, nullptr);

	D3D12_RESOURCE_BARRIER presentation_state_barrier {};
	presentation_state_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentation_state_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	presentation_state_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	presentation_state_barrier.Transition.pResource = current_resources.swap_chain_buffer.get();
	m_command_list->ResourceBarrier(1, &presentation_state_barrier);

	winrt::check_hresult(m_command_list->Close());
	ID3D12CommandList* const generic_list_pointer {m_command_list.get()};
	m_queue->ExecuteCommandLists(1, &generic_list_pointer);

	present(*m_swap_chain);
	winrt::check_hresult(m_queue->Signal(m_fence.get(), ++m_fence_current_value));
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

void matrix::graphics_engine_state::signal_size_change()
{
	m_frame_resources = {};
	resize(*m_swap_chain);
	m_frame_resources = create_frame_resources(*m_device, *m_rtv_heap, *m_swap_chain);
}
