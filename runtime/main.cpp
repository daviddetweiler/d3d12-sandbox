#include "pch.h"

#include "graphics_engine_state.h"

namespace sandbox {
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
				m_for_client {&m_client_data.back()}
			{
			}

			const auto& swap_buffers()
			{
				std::swap(m_for_host, m_for_client);
				reset(*m_for_host);
				return *m_for_client;
			}

			void enqueue(const input_event& event) { m_for_host->input_events.emplace_back(event); }
			void request_exit() { m_for_host->exit_requested = true; }
			void invalidate_size() { m_for_host->size_invalidated = true; }

		private:
			std::array<host_client_data, 2> m_client_data;
			gsl::not_null<host_client_data*> m_for_host;
			gsl::not_null<host_client_data*> m_for_client;
		};

		constexpr DWORD confirm_exit {WM_USER};
		constexpr DWORD client_ready {WM_USER + 1};

		GSL_SUPPRESS(type .1) // reinterpret_cast<>() is inherently required for some API operations
		GSL_SUPPRESS(f .6) // Caller cannot handle an exception being thrown, so we must call std::terminate() on
						   // possible exceptions
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

		GSL_SUPPRESS(type .1) // Casts required by API
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
			const WNDCLASSEXW window_class {
				.cbSize {sizeof(window_class)},
				.lpfnWndProc {handle_host_creation},
				.hInstance {instance},
				.hCursor {winrt::check_pointer(LoadCursorW(nullptr, IDC_ARROW))},
				.lpszClassName {L"sandbox::host_window"},
			};

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

		DirectX::XMMATRIX map_to_camera_transform(WPARAM key)
		{
			static constexpr auto linear_speed = 0.03f;
			static constexpr auto angular_speed = 0.04f;
			static const auto forward_translate = DirectX::XMMatrixTranslation(0.0f, 0.0f, -linear_speed);
			static const auto back_translate = DirectX::XMMatrixTranslation(0.0f, 0.0f, linear_speed);
			static const auto left_rotate = DirectX::XMMatrixRotationY(angular_speed);
			static const auto right_rotate = DirectX::XMMatrixRotationY(-angular_speed);
			static const auto up_rotate = DirectX::XMMatrixRotationX(angular_speed);
			static const auto down_rotate = DirectX::XMMatrixRotationX(-angular_speed);
			static const auto left_roll = DirectX::XMMatrixRotationZ(-angular_speed);
			static const auto right_roll = DirectX::XMMatrixRotationZ(angular_speed);
			static const auto pan_left = DirectX::XMMatrixTranslation(linear_speed, 0.0f, 0.0f);
			static const auto pan_right = DirectX::XMMatrixTranslation(-linear_speed, 0.0f, 0.0f);
			static const auto pan_up = DirectX::XMMatrixTranslation(0.0f, -linear_speed, 0.0f);
			static const auto pan_down = DirectX::XMMatrixTranslation(0.0f, linear_speed, 0.0f);
			switch (key) {
			case VK_UP:
				return pan_up;

			case 'W':
				return forward_translate;

			case VK_DOWN:
				return pan_down;

			case 'S':
				return back_translate;

			case VK_LEFT:
				return pan_left;

			case 'A':
				return left_rotate;

			case VK_RIGHT:
				return pan_right;

			case 'D':
				return right_rotate;

			case 'R':
				return up_rotate;

			case 'F':
				return down_rotate;

			case 'Q':
				return left_roll;

			case 'E':
				return right_roll;

			default:
				return DirectX::XMMatrixIdentity();
			}
		}

		void flush_message_queue() noexcept
		{
			MSG message {};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}

		void do_update_loop(HWND host_window, host_atomic_state& client_data, const std::filesystem::path& filepath)
		{
			bool is_first_frame {true};
			auto view_matrix = DirectX::XMMatrixIdentity();
			render_mode type = render_mode::object_view;
			graphics_engine_state renderer {host_window, filepath};
			bool snapshot {};
			while (true) {
				using clock = std::chrono::high_resolution_clock;
				const auto start = clock::now();

				flush_message_queue();
				const auto& current_state = client_data.swap_buffers();
				if (current_state.exit_requested)
					break;

				if (current_state.size_invalidated)
					renderer.signal_size_change();

				for (const auto& event : current_state.input_events) {
					if (event.type == input_event_type::key_pressed) {
						switch (event.w) {
						case '1':
							type = render_mode::debug_grid;
							break;

						case '2':
							type = render_mode::object_view;
							break;

						case '3':
							type = render_mode::wireframe_view;
							break;

						case VK_ESCAPE:
							snapshot = true;
							break;

						default:
							view_matrix *= map_to_camera_transform(event.w);
							break;
						}
					}
				}

				renderer.render(type, view_matrix);
				if (is_first_frame) {
					SendMessageW(host_window, client_ready, 0, 0);
					is_first_frame = false;
				}

				const auto stop = clock::now();
				if (snapshot) {
					std::wstringstream debug_message {};
					debug_message << std::chrono::duration_cast<std::chrono::microseconds>(stop - start) << "\n";
					OutputDebugStringW(debug_message.str().c_str());
					snapshot = false;
				}
			}
		}
	}
}

int wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR command_line, _In_ int)
{
	int argc;
	const auto argv = CommandLineToArgvW(command_line, &argc);
	const gsl::span arguments {argv, gsl::narrow_cast<std::size_t>(argc)};
	if (argc < 1)
		return 1;

	sandbox::host_atomic_state ui_state {};
	const auto host_window = sandbox::create_host_window(instance, ui_state);
	sandbox::do_update_loop(host_window, ui_state, arguments[0]);
	SendMessageW(host_window, sandbox::confirm_exit, 0, 0);

	return sandbox::handle_messages_until_quit();
}
