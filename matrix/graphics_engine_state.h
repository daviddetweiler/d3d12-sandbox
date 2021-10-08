#pragma once

#include "pch.h"

namespace matrix {
	struct per_frame_resources {
		D3D12_CPU_DESCRIPTOR_HANDLE render_target_view_handle {};
		winrt::com_ptr<ID3D12Resource> swap_chain_buffer {};
		winrt::com_ptr<ID3D12CommandAllocator> allocator {};
		winrt::com_ptr<ID3D12GraphicsCommandList> commands {};
	};

	struct root_signature_table {
		const winrt::com_ptr<ID3D12RootSignature> default_signature; // For lack of a better name
	};

	class graphics_engine_state {
	public:
		graphics_engine_state(HWND target_window);
		void update();
		void signal_size_change();

		GSL_SUPPRESS(f .6)
		~graphics_engine_state() noexcept;

		graphics_engine_state(const graphics_engine_state&) = delete;
		graphics_engine_state& operator=(const graphics_engine_state&) = delete;
		graphics_engine_state(const graphics_engine_state&&) = delete;
		graphics_engine_state& operator=(const graphics_engine_state&&) = delete;

	private:
		const winrt::com_ptr<ID3D12Device4> m_device;
		const winrt::com_ptr<ID3D12CommandQueue> m_queue;
		const winrt::com_ptr<IDXGISwapChain3> m_swap_chain;
		const winrt::com_ptr<ID3D12DescriptorHeap> m_rtv_heap;
		const winrt::com_ptr<ID3D12DescriptorHeap> m_dsv_heap;
		const root_signature_table m_root_signatures;
		const winrt::com_ptr<ID3D12PipelineState> m_debug_grid_pass;

		winrt::com_ptr<ID3D12Resource> m_depth_buffer;
		std::array<per_frame_resources, 2> m_frame_resources;

		std::uint64_t m_fence_current_value;
		const winrt::com_ptr<ID3D12Fence> m_fence;

		graphics_engine_state(IDXGIFactory6& factory, HWND target_window);

		void wait_for_idle();
		const per_frame_resources& wait_for_frame();
		void signal_frame_submission();
	};
}
