#include "pch.h"

namespace matrix {
	namespace {
		int handle_messages_until_quit()
		{
			MSG next_message {};
			while (const auto status_code = GetMessageW(&next_message, nullptr, 0, 0)) {
				if (status_code == -1)
					winrt::throw_last_error();

				TranslateMessage(&next_message);
				DispatchMessageW(&next_message);
			}

			return gsl::narrow<int>(next_message.wParam);
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

			// The returned span of input events will be invalidated upon the next call to swap_buffers
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

			auto get()
			{
				const std::lock_guard swap_lock {m_swap_mutex};
				return gsl::span {*m_current_buffer_for_sink};
			}

		private:
			static constexpr auto initial_capacity = 1024;

			std::array<std::vector<input_event>, 2> m_event_buffers;
			gsl::not_null<std::vector<input_event>*> m_current_buffer_for_source;
			gsl::not_null<std::vector<input_event>*> m_current_buffer_for_sink;
			spinlock m_swap_mutex;
		};

		struct host_window_client_data {
			event exit_requested {};
			event size_invalidated {};
			input_event_queue input_events {throwing_default {}};
		};

		struct host_window_state {
			event exit_confirmed;
			host_window_client_data client_data;
		};

		GSL_SUPPRESS(type .1)
		GSL_SUPPRESS(f .6)
		LRESULT handle_host_update(HWND window, UINT message, WPARAM w, LPARAM l) noexcept
		{
			const gsl::not_null state = reinterpret_cast<host_window_state*>(GetWindowLongPtrW(window, GWLP_USERDATA));
			auto& client_data = state->client_data;
			if (state->exit_confirmed)
				winrt::check_bool(DestroyWindow(window));

			switch (message) {
			case WM_CLOSE:
				client_data.exit_requested.signal();
				return 0;

			case WM_SIZE:
				client_data.size_invalidated.signal();
				return 0;

			case WM_KEYUP:
				client_data.input_events.enqueue({input_event_type::key_released, w, l});
				return 0;

			case WM_KEYDOWN:
				client_data.input_events.enqueue({input_event_type::key_pressed, w, l});
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
			window_class.lpszClassName = L"matrix::host_window";
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

		void do_client_thread(host_window_client_data& client_data)
		{
			while (!client_data.exit_requested) {
				client_data.input_events.swap_buffers();
				std::this_thread::yield();
			}
		}
	}
}

int wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	matrix::host_window_state ui_state {};
	matrix::create_host_window(instance, ui_state);
	const std::jthread client_thread {[&ui_state]() {
		matrix::do_client_thread(ui_state.client_data);
		ui_state.exit_confirmed.signal();
	}};

	return matrix::handle_messages_until_quit();
}
