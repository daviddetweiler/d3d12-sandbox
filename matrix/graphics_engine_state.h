#pragma once

#include "pch.h"

namespace matrix {
	struct per_frame_resources {
		D3D12_CPU_DESCRIPTOR_HANDLE render_target_view_handle;
		winrt::com_ptr<ID3D12Resource> swap_chain_buffer;
	};

	class graphics_engine_state {
	public:
		graphics_engine_state(HWND target_window);
		void update();
		void signal_size_change();

	private:
		const winrt::com_ptr<ID3D12Device4> m_device;
		const winrt::com_ptr<ID3D12CommandQueue> m_queue;
		const winrt::com_ptr<IDXGISwapChain3> m_swap_chain;
		const winrt::com_ptr<ID3D12DescriptorHeap> m_rtv_heap;
		const winrt::com_ptr<ID3D12CommandAllocator> m_allocator;
		const winrt::com_ptr<ID3D12GraphicsCommandList> m_command_list;

		std::array<per_frame_resources, 2> m_frame_resources;

		std::uint64_t m_fence_current_value;
		const winrt::com_ptr<ID3D12Fence> m_fence;

		graphics_engine_state(IDXGIFactory6& factory, HWND target_window);
	};
}
