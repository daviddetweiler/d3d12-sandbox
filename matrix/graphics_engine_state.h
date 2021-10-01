#pragma once

#include "pch.h"

namespace matrix {
	class graphics_engine_state {
	public:
		graphics_engine_state(HWND target_window);
		void update();
		void signal_size_change();

	private:
		const winrt::com_ptr<ID3D12Device> m_device;
		const winrt::com_ptr<ID3D12CommandQueue> m_queue;
		const winrt::com_ptr<IDXGISwapChain> m_swap_chain;

		std::uint64_t m_fence_current_value;
		const winrt::com_ptr<ID3D12Fence> m_fence;

		graphics_engine_state(IDXGIFactory6& factory, HWND target_window);
	};
}
