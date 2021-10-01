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

			return winrt::capture<ID3D12Device>(D3D12CreateDevice, selected_adapter.get(), D3D_FEATURE_LEVEL_12_1);
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

			return swap_chain;
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
	m_fence_current_value {},
	m_fence {create_fence(*m_device, m_fence_current_value)}
{
}

void matrix::graphics_engine_state::update()
{
	present(*m_swap_chain);
	winrt::check_hresult(m_queue->Signal(m_fence.get(), ++m_fence_current_value));
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

void matrix::graphics_engine_state::signal_size_change() { resize(*m_swap_chain); }
