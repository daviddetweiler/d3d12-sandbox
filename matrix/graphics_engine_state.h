#pragma once

#include "pch.h"

namespace matrix {
	struct per_frame_resources {
		winrt::com_ptr<ID3D12CommandAllocator> allocator {};
		winrt::com_ptr<ID3D12GraphicsCommandList> command_list {};
		
		// Swap chain dependent

		D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_view {};
		winrt::com_ptr<ID3D12Resource> backbuffer {};
		D3D12_CPU_DESCRIPTOR_HANDLE depth_buffer_view {};
		winrt::com_ptr<ID3D12Resource> depth_buffer {};
	};

	struct root_signature_table {
		const winrt::com_ptr<ID3D12RootSignature> default_signature; // For lack of a better name
	};

	struct pipeline_state_table {
		const winrt::com_ptr<ID3D12PipelineState> debug_grid_pipeline;
		const winrt::com_ptr<ID3D12PipelineState> object_pipeline;
	};

	enum class render_mode { debug_grid, object_view };

	class graphics_engine_state {
	public:
		graphics_engine_state(HWND target_window);
		void update(render_mode type, const DirectX::XMMATRIX& view_matrix);
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
		const pipeline_state_table m_pipelines;

		std::array<per_frame_resources, 2> m_frame_resources;

		std::uint64_t m_fence_current_value;
		const winrt::com_ptr<ID3D12Fence> m_fence;

		DirectX::XMMATRIX m_projection_matrix;

		graphics_engine_state(IDXGIFactory6& factory, HWND target_window);

		void wait_for_idle();
		const per_frame_resources& wait_for_frame();
		void signal_frame_submission();
	};
}
