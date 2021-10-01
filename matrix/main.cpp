#include "pch.h"

namespace matrix {
	namespace {
		// Returns false once the quit message has been posted
		bool flush_message_queue() noexcept
		{
			MSG next_message {};
			while (PeekMessageW(&next_message, nullptr, 0, 0, PM_REMOVE)) {
				if (next_message.message == WM_QUIT)
					return false;

				TranslateMessage(&next_message);
				DispatchMessageW(&next_message);
			}

			return true;
		}

		class event {
		public:
			event() noexcept : m_not_signalled {} { m_not_signalled.test_and_set(); }
			void signal() noexcept { m_not_signalled.clear(); }
			operator bool() noexcept { return !m_not_signalled.test_and_set(); }

		private:
			std::atomic_flag m_not_signalled;
		};

		enum class input_event_type { key_pressed, key_released };

		struct input_event {
			input_event_type type;
			WPARAM w;
			LPARAM l;
		};

		class spinlock {
		public:
			void lock() noexcept
			{
				while (m_locked.test_and_set(std::memory_order_acquire))
					_mm_pause();
			}

			void unlock() noexcept { m_locked.clear(); }

		private:
			std::atomic_flag m_locked = ATOMIC_FLAG_INIT;
		};

		struct throwing_default {
		};

		class input_event_queue {
			class temporary_range;

		public:
			input_event_queue(throwing_default) :
				m_event_buffers {},
				m_current_buffer_for_source {&m_event_buffers.front()},
				m_current_buffer_for_sink {&m_event_buffers.back()},
				m_swap_mutex {}
			{
				m_current_buffer_for_source->reserve(initial_capacity);
				m_current_buffer_for_sink->reserve(initial_capacity);
			}

			// The returned span of input events will be invalidated upon the next call
			gsl::span<const input_event> swap_buffers()
			{
				const std::lock_guard swap_lock {m_swap_mutex};
				std::swap(m_current_buffer_for_source, m_current_buffer_for_sink);
				m_current_buffer_for_source->clear();
				return {*m_current_buffer_for_sink};
			}

			void enqueue(const input_event& event)
			{
				const std::lock_guard swap_lock {m_swap_mutex};
				m_current_buffer_for_source->emplace_back(event);
			}

		private:
			static constexpr auto initial_capacity = 1024;

			std::array<std::vector<input_event>, 2> m_event_buffers;
			gsl::not_null<std::vector<input_event>*> m_current_buffer_for_source;
			gsl::not_null<std::vector<input_event>*> m_current_buffer_for_sink;
			spinlock m_swap_mutex;
		};

		struct host_window_state {
			event exit_confirmed;
			event exit_requested {};
			event size_invalidated {};
			input_event_queue input_events {throwing_default {}};
		};

		GSL_SUPPRESS(type .1)
		GSL_SUPPRESS(f .6)
		LRESULT handle_host_update(HWND window, UINT message, WPARAM w, LPARAM l) noexcept
		{
			const gsl::not_null state = reinterpret_cast<host_window_state*>(GetWindowLongPtrW(window, GWLP_USERDATA));
			if (state->exit_confirmed)
				winrt::check_bool(DestroyWindow(window));

			switch (message) {
			case WM_CLOSE:
				state->exit_requested.signal();
				return 0;

			case WM_SIZE:
				state->size_invalidated.signal();
				return 0;

			case WM_KEYUP:
				state->input_events.enqueue({input_event_type::key_released, w, l});
				return 0;

			case WM_KEYDOWN:
				state->input_events.enqueue({input_event_type::key_pressed, w, l});
				return 0;

			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;

			default:
				return DefWindowProcW(window, message, w, l);
			}
		}

		GSL_SUPPRESS(type .1)
		LRESULT handle_host_creation(HWND window, UINT message, WPARAM w, LPARAM l) noexcept
		{
			switch (message) {
			case WM_CREATE: {
				const gsl::not_null state = reinterpret_cast<LPCREATESTRUCTW>(l)->lpCreateParams;
				SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
				SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(handle_host_update));
				return 0;
			}

			default:
				return DefWindowProcW(window, message, w, l);
			}
		}

		HWND create_host_window(HINSTANCE instance, host_window_state& state)
		{
			WNDCLASSEXW window_class {};
			window_class.cbSize = sizeof(window_class);
			window_class.hCursor = winrt::check_pointer(LoadCursorW(nullptr, IDC_ARROW));
			window_class.hInstance = instance;
			window_class.lpszClassName = L"host_window";
			window_class.lpfnWndProc = handle_host_creation;
			winrt::check_bool(RegisterClassExW(&window_class));

			return winrt::check_pointer(CreateWindowExW(
				WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
				window_class.lpszClassName,
				L"",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				nullptr,
				nullptr,
				window_class.hInstance,
				&state));
		}

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

		void do_client_thread(HWND host_window, host_window_state& client_data)
		{
			const auto dxgi_factory = create_dxgi_factory();
			const auto device = create_gpu_device(*dxgi_factory);
			const auto command_queue = create_command_queue(*device);
			const auto swap_chain = create_swap_chain(*dxgi_factory, *command_queue, host_window);
			std::uint64_t frame_fence_value {};
			const auto frame_fence = create_fence(*device, frame_fence_value);
			while (!client_data.exit_requested) {
				for (const auto& event : client_data.input_events.swap_buffers()) {
					if (event.type == input_event_type::key_pressed && event.w == VK_ESCAPE)
						client_data.exit_requested.signal();
				}

				present(*swap_chain);
				winrt::check_hresult(command_queue->Signal(frame_fence.get(), ++frame_fence_value));
				while (frame_fence->GetCompletedValue() < frame_fence_value)
					_mm_pause();
			}
		}

		void resize(IDXGISwapChain& swap_chain)
		{
			OutputDebugStringW(L"[note] resize triggered\n");
			winrt::check_hresult(swap_chain.ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
		}
	}
}

int wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	using namespace matrix;

	host_window_state host_state {};
	const auto host_window = create_host_window(instance, host_state);
	const auto dxgi_factory = create_dxgi_factory();
	const auto device = create_gpu_device(*dxgi_factory);
	const auto command_queue = create_command_queue(*device);
	const auto swap_chain = create_swap_chain(*dxgi_factory, *command_queue, host_window);
	std::uint64_t frame_fence_value {};
	const auto frame_fence = create_fence(*device, frame_fence_value);
	while (flush_message_queue()) {
		if (host_state.exit_requested)
			host_state.exit_confirmed.signal();

		if (host_state.size_invalidated)
			resize(*swap_chain);

		for (const auto& event : host_state.input_events.swap_buffers()) {
			if (event.type == matrix::input_event_type::key_pressed && event.w == VK_ESCAPE)
				host_state.exit_requested.signal();
		}

		present(*swap_chain);
		winrt::check_hresult(command_queue->Signal(frame_fence.get(), ++frame_fence_value));
		while (frame_fence->GetCompletedValue() < frame_fence_value)
			_mm_pause();
	}

	return 0;
}
