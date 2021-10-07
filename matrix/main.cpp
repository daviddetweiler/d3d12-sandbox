#include "pch.h"

#include "graphics_engine_state.h"
#include "wavefront_loader.h"

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

		struct host_client_data {
			std::vector<input_event> input_events;
			bool exit_requested;
			bool size_invalidated;
		};

		void reset(host_client_data& client_data) noexcept
		{
			client_data.input_events.clear();
			client_data.exit_requested = false;
			client_data.size_invalidated = false;
		}

		class host_atomic_state {
		public:
			host_atomic_state() noexcept :
				m_client_data {},
				m_for_host {&m_client_data.front()},
				m_for_client {&m_client_data.back()},
				m_swap_lock {}
			{
			}

			const auto& swap_buffers()
			{
				const std::lock_guard lock {m_swap_lock};
				std::swap(m_for_host, m_for_client);
				reset(*m_for_host);
				return *m_for_client;
			}

			void enqueue(const input_event& event)
			{
				const std::lock_guard swap_lock {m_swap_lock};
				m_for_host->input_events.emplace_back(event);
			}

			void request_exit()
			{
				const std::lock_guard swap_lock {m_swap_lock};
				m_for_host->exit_requested = true;
			}

			void invalidate_size()
			{
				const std::lock_guard swap_lock {m_swap_lock};
				m_for_host->size_invalidated = true;
			}

		private:
			std::array<host_client_data, 2> m_client_data;
			gsl::not_null<host_client_data*> m_for_host;
			gsl::not_null<host_client_data*> m_for_client;
			spinlock m_swap_lock;
		};

		constexpr DWORD confirm_exit {WM_USER};
		constexpr DWORD client_ready {WM_USER + 1};

		GSL_SUPPRESS(type .1)
		GSL_SUPPRESS(f .6)
		LRESULT handle_host_update(HWND window, UINT message, WPARAM w, LPARAM l) noexcept
		{
			const gsl::not_null client_data
				= reinterpret_cast<host_atomic_state*>(GetWindowLongPtrW(window, GWLP_USERDATA));

			switch (message) {
			case WM_CLOSE:
				client_data->request_exit();
				return 0;

			case WM_SIZE:
				client_data->invalidate_size();
				return 0;

			case WM_KEYUP:
				client_data->enqueue({input_event_type::key_released, w, l});
				return 0;

			case WM_KEYDOWN:
				client_data->enqueue({input_event_type::key_pressed, w, l});
				return 0;

			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;

			case client_ready:
				ShowWindow(window, SW_SHOW);
				return 0;

			case confirm_exit:
				winrt::check_bool(DestroyWindow(window));
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

		HWND create_host_window(HINSTANCE instance, host_atomic_state& state)
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
				WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				nullptr,
				nullptr,
				window_class.hInstance,
				&state));
		}

		void do_client_thread(HWND host_window, host_atomic_state& client_data)
		{
			bool is_first_frame {true};
			const auto object = load_wavefront("cube.obj");
			static_cast<void>(object);
			graphics_engine_state graphics_state {host_window};
			while (true) {
				const auto& current_state = client_data.swap_buffers();
				if (current_state.exit_requested)
					break;

				if (current_state.size_invalidated)
					graphics_state.signal_size_change();

				if (!current_state.input_events.empty())
					OutputDebugStringW(L"[note] input events available\n");

				graphics_state.update();

				if (is_first_frame) {
					SendMessageW(host_window, client_ready, 0, 0);
					is_first_frame = false;
				}
			}
		}
	}
}

int wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	matrix::host_atomic_state ui_state {};
	const auto host_window = matrix::create_host_window(instance, ui_state);
	const std::jthread client_thread {[&ui_state, host_window]() {
		matrix::do_client_thread(host_window, ui_state);
		SendMessageW(host_window, matrix::confirm_exit, 0, 0);
	}};

	return matrix::handle_messages_until_quit();
}
