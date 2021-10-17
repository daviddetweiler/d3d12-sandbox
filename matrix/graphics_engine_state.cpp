#include "pch.h"

#include "graphics_engine_state.h"

#include "shader_loading.h"
#include "wavefront_loader.h"

namespace matrix {
	namespace {
		auto create_dxgi_factory()
		{
			return winrt::capture<IDXGIFactory6>(
				CreateDXGIFactory2,
				IsDebuggerPresent() ? DXGI_CREATE_FACTORY_DEBUG : 0);
		}

		auto create_gpu_device(IDXGIFactory6& dxgi_factory)
		{
			if (IsDebuggerPresent())
				winrt::capture<ID3D12Debug>(D3D12GetDebugInterface)->EnableDebugLayer();

			const auto selected_adapter = winrt::capture<IUnknown>(
				&dxgi_factory,
				&IDXGIFactory6::EnumAdapterByGpuPreference,
				0,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);

			return winrt::capture<ID3D12Device4>(D3D12CreateDevice, selected_adapter.get(), D3D_FEATURE_LEVEL_12_1);
		}

		auto create_command_queue(ID3D12Device& device)
		{
			D3D12_COMMAND_QUEUE_DESC description {};
			return winrt::capture<ID3D12CommandQueue>(&device, &ID3D12Device::CreateCommandQueue, &description);
		}

		auto create_swap_chain(IDXGIFactory3& factory, ID3D12CommandQueue& presenter_queue, HWND target_window)
		{
			const DXGI_SWAP_CHAIN_DESC1 description {
				.Format {DXGI_FORMAT_R8G8B8A8_UNORM},
				.SampleDesc {.Count {1}},
				.BufferUsage {DXGI_USAGE_RENDER_TARGET_OUTPUT},
				.BufferCount {2},
				.SwapEffect {DXGI_SWAP_EFFECT_FLIP_DISCARD},
			};

			winrt::com_ptr<IDXGISwapChain1> swap_chain {};
			winrt::check_hresult(factory.CreateSwapChainForHwnd(
				&presenter_queue,
				target_window,
				&description,
				nullptr,
				nullptr,
				swap_chain.put()));

			// We do not currently support exclusive-mode fullscreen
			winrt::check_hresult(factory.MakeWindowAssociation(target_window, DXGI_MWA_NO_ALT_ENTER));

			return swap_chain.as<IDXGISwapChain3>();
		}

		void present(IDXGISwapChain& swap_chain) { winrt::check_hresult(swap_chain.Present(1, 0)); }

		auto create_fence(ID3D12Device& device, std::uint64_t initial_value)
		{
			return winrt::capture<ID3D12Fence>(
				&device,
				&ID3D12Device::CreateFence,
				initial_value,
				D3D12_FENCE_FLAG_NONE);
		}

		void resize(IDXGISwapChain& swap_chain)
		{
			winrt::check_hresult(swap_chain.ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
		}

		auto
		create_descriptor_heap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE descriptor_type, unsigned int capacity)
		{
			const D3D12_DESCRIPTOR_HEAP_DESC description {.Type {descriptor_type}, .NumDescriptors {capacity}};
			return winrt::capture<ID3D12DescriptorHeap>(&device, &ID3D12Device::CreateDescriptorHeap, &description);
		}

		D3D12_RESOURCE_BARRIER create_transition_barrier(
			ID3D12Resource& resource,
			D3D12_RESOURCE_STATES state_before,
			D3D12_RESOURCE_STATES state_after) noexcept
		{
			return {
				.Type {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION},
				.Transition {.pResource {&resource}, .StateBefore {state_before}, .StateAfter {state_after}},
			};
		}

		template <typename... list_types>
		void execute_command_lists(ID3D12CommandQueue& queue, list_types&... command_lists)
		{
			std::array<ID3D12CommandList*, sizeof...(command_lists)> list_pointers {&command_lists...};
			queue.ExecuteCommandLists(gsl::narrow<UINT>(list_pointers.size()), list_pointers.data());
		}

		template <typename... barrier_types>
		void submit_resource_barriers(ID3D12GraphicsCommandList& command_list, barrier_types... barriers)
		{
			const std::array<D3D12_RESOURCE_BARRIER, sizeof...(barrier_types)> all_barriers {barriers...};
			command_list.ResourceBarrier(gsl::narrow<UINT>(all_barriers.size()), all_barriers.data());
		}

		void clear_render_target(
			ID3D12GraphicsCommandList& command_list,
			D3D12_CPU_DESCRIPTOR_HANDLE view_handle,
			float red = 0.0f,
			float green = 0.0f,
			float blue = 0.0f,
			float alpha = 1.0f)
		{
			const std::array color {red, green, blue, alpha};
			command_list.ClearRenderTargetView(view_handle, color.data(), 0, nullptr);
		}

		auto create_debug_grid_pipeline_state(ID3D12Device& device, const root_signature_table& root_signatures)
		{
			const auto vertex_shader = load_compiled_shader(L"debug_grid.cso");
			const auto pixel_shader = load_compiled_shader(L"all_white.cso");
			const D3D12_GRAPHICS_PIPELINE_STATE_DESC description {
				.pRootSignature {root_signatures.default_signature.get()},
				.VS {.pShaderBytecode {vertex_shader.data()}, .BytecodeLength {vertex_shader.size()}},
				.PS {.pShaderBytecode {pixel_shader.data()}, .BytecodeLength {pixel_shader.size()}},
				.BlendState {.RenderTarget {{.RenderTargetWriteMask {D3D12_COLOR_WRITE_ENABLE_ALL}}}},
				.SampleMask {D3D12_DEFAULT_SAMPLE_MASK},
				.RasterizerState {
					.FillMode {D3D12_FILL_MODE_SOLID},
					.CullMode {D3D12_CULL_MODE_BACK},
					.DepthClipEnable {true},
				},
				.DepthStencilState {
					.DepthEnable {true},
					.DepthWriteMask {D3D12_DEPTH_WRITE_MASK_ALL},
					.DepthFunc {D3D12_COMPARISON_FUNC_LESS},
				},
				.PrimitiveTopologyType {D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE},
				.NumRenderTargets {1},
				.RTVFormats {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
				.DSVFormat {DXGI_FORMAT_D32_FLOAT},
				.SampleDesc {.Count {1}},
			};

			return winrt::capture<ID3D12PipelineState>(
				&device,
				&ID3D12Device::CreateGraphicsPipelineState,
				&description);
		}

		auto create_object_pipeline_state(ID3D12Device& device, const root_signature_table& root_signatures)
		{
			const auto vertex_shader = load_compiled_shader(L"debug_colors.cso");
			const auto pixel_shader = load_compiled_shader(L"vertex_color_passthrough.cso");
			const D3D12_INPUT_ELEMENT_DESC position {
				.SemanticName {"POSITION"},
				.Format {DXGI_FORMAT_R32G32B32_FLOAT},
				.AlignedByteOffset {D3D12_APPEND_ALIGNED_ELEMENT},
				.InputSlotClass {D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
			};

			const D3D12_GRAPHICS_PIPELINE_STATE_DESC description {
				.pRootSignature {root_signatures.default_signature.get()},
				.VS {.pShaderBytecode {vertex_shader.data()}, .BytecodeLength {vertex_shader.size()}},
				.PS {.pShaderBytecode {pixel_shader.data()}, .BytecodeLength {pixel_shader.size()}},
				.BlendState {.RenderTarget {{.RenderTargetWriteMask {D3D12_COLOR_WRITE_ENABLE_ALL}}}},
				.SampleMask {D3D12_DEFAULT_SAMPLE_MASK},
				.RasterizerState {
					.FillMode {D3D12_FILL_MODE_SOLID},
					.CullMode {D3D12_CULL_MODE_NONE},
					.DepthClipEnable {true},
				},
				.DepthStencilState {
					.DepthEnable {true},
					.DepthWriteMask {D3D12_DEPTH_WRITE_MASK_ALL},
					.DepthFunc {D3D12_COMPARISON_FUNC_LESS},
				},
				.InputLayout {.pInputElementDescs {&position}, .NumElements {1}},
				.PrimitiveTopologyType {D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE},
				.NumRenderTargets {1},
				.RTVFormats {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
				.DSVFormat {DXGI_FORMAT_D32_FLOAT},
				.SampleDesc {.Count {1}},
			};

			return winrt::capture<ID3D12PipelineState>(
				&device,
				&ID3D12Device::CreateGraphicsPipelineState,
				&description);
		}

		auto create_root_signature(ID3D12Device& device)
		{
			const D3D12_ROOT_PARAMETER constants {
				.ParameterType {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS},
				.Constants {.Num32BitValues {4 * 4 * 2}},
			};

			const D3D12_ROOT_SIGNATURE_DESC info {
				.NumParameters {1},
				.pParameters {&constants},
				.Flags {D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT},
			};

			winrt::com_ptr<ID3DBlob> result {};
			winrt::com_ptr<ID3DBlob> error {};
			winrt::check_hresult(
				D3D12SerializeRootSignature(&info, D3D_ROOT_SIGNATURE_VERSION_1, result.put(), error.put()));

			return winrt::capture<ID3D12RootSignature>(
				&device,
				&ID3D12Device::CreateRootSignature,
				0,
				result->GetBufferPointer(),
				result->GetBufferSize());
		}

		void maximize_rasterizer(ID3D12GraphicsCommandList& list, ID3D12Resource& target)
		{
			const auto info = target.GetDesc();
			const D3D12_RECT scissor {
				.right {gsl::narrow<long>(info.Width)},
				.bottom {gsl::narrow<long>(info.Height)},
			};

			const D3D12_VIEWPORT viewport {
				.Width {gsl::narrow<float>(info.Width)},
				.Height {gsl::narrow<float>(info.Height)},
				.MaxDepth {1.0f},
			};

			list.RSSetScissorRects(1, &scissor);
			list.RSSetViewports(1, &viewport);
		}

		struct extent2d {
			UINT width;
			UINT height;
		};

		extent2d get_extent(IDXGISwapChain& swap_chain)
		{
			DXGI_SWAP_CHAIN_DESC description {};
			winrt::check_hresult(swap_chain.GetDesc(&description));
			return {.width {description.BufferDesc.Width}, .height {description.BufferDesc.Height}};
		}

		auto create_depth_buffer(ID3D12Device& device, D3D12_CPU_DESCRIPTOR_HANDLE dsv, const extent2d& size)
		{
			const D3D12_HEAP_PROPERTIES properties {.Type {D3D12_HEAP_TYPE_DEFAULT}};
			const D3D12_RESOURCE_DESC info {
				.Dimension {D3D12_RESOURCE_DIMENSION_TEXTURE2D},
				.Width {size.width},
				.Height {size.height},
				.DepthOrArraySize {1},
				.MipLevels {1},
				.Format {DXGI_FORMAT_D32_FLOAT},
				.SampleDesc {.Count {1}},
				.Flags {D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL},
			};

			const D3D12_CLEAR_VALUE clear_value {.Format {info.Format}, .DepthStencil {.Depth {1.0f}}};
			const auto buffer = winrt::capture<ID3D12Resource>(
				&device,
				&ID3D12Device::CreateCommittedResource,
				&properties,
				D3D12_HEAP_FLAG_NONE,
				&info,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&clear_value);

			const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_info {
				.Format {info.Format},
				.ViewDimension {D3D12_DSV_DIMENSION_TEXTURE2D},
			};

			device.CreateDepthStencilView(buffer.get(), &dsv_info, dsv);

			return buffer;
		}

		root_signature_table create_root_signatures(ID3D12Device& device)
		{
			return {.default_signature {create_root_signature(device)}};
		}

		pipeline_state_table create_pipeline_states(ID3D12Device& device, const root_signature_table& root_signatures)
		{
			return {
				.debug_grid_pipeline {create_debug_grid_pipeline_state(device, root_signatures)},
				.object_pipeline {create_object_pipeline_state(device, root_signatures)},
			};
		}

		// TODO: should I be moved in-class?
		void record_debug_grid_commands(
			const per_frame_resources& resources,
			const root_signature_table& root_signatures,
			const DirectX::XMMATRIX& view,
			const DirectX::XMMATRIX& projection)
		{
			auto& command_list = *resources.command_list;
			auto& backbuffer = *resources.backbuffer;
			const auto& backbuffer_view = resources.backbuffer_view;
			const auto depth_buffer_view = resources.depth_buffer_view;

			command_list.SetGraphicsRootSignature(root_signatures.default_signature.get());
			command_list.SetGraphicsRoot32BitConstants(0, 16, &view, 0);
			command_list.SetGraphicsRoot32BitConstants(0, 16, &projection, 16);

			command_list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
			maximize_rasterizer(command_list, backbuffer);
			command_list.OMSetRenderTargets(1, &backbuffer_view, false, &depth_buffer_view);

			command_list.ClearDepthStencilView(depth_buffer_view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			submit_resource_barriers(
				command_list,
				create_transition_barrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

			clear_render_target(command_list, backbuffer_view);
			command_list.DrawInstanced(2, 18, 0, 0);
			submit_resource_barriers(
				command_list,
				create_transition_barrier(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));
		}

		// TODO: should I be moved in-class?
		void record_object_view_commands(
			const per_frame_resources& resources,
			const root_signature_table& root_signatures,
			const DirectX::XMMATRIX& view,
			const DirectX::XMMATRIX& projection,
			const loaded_geometry& object)
		{
			auto& command_list = *resources.command_list;
			auto& backbuffer = *resources.backbuffer;
			const auto& backbuffer_view = resources.backbuffer_view;
			const auto depth_buffer_view = resources.depth_buffer_view;

			command_list.SetGraphicsRootSignature(root_signatures.default_signature.get());
			command_list.SetGraphicsRoot32BitConstants(0, 16, &view, 0);
			command_list.SetGraphicsRoot32BitConstants(0, 16, &projection, 16);

			command_list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			command_list.IASetIndexBuffer(&object.index_view);
			command_list.IASetVertexBuffers(0, 1, &object.vertex_view);
			maximize_rasterizer(command_list, backbuffer);
			command_list.OMSetRenderTargets(1, &backbuffer_view, false, &depth_buffer_view);

			command_list.ClearDepthStencilView(depth_buffer_view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			submit_resource_barriers(
				command_list,
				create_transition_barrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

			clear_render_target(command_list, backbuffer_view);
			command_list.DrawIndexedInstanced(object.size, 1, 0, 0, 0);
			submit_resource_barriers(
				command_list,
				create_transition_barrier(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));
		}

		DirectX::XMMATRIX compute_projection(IDXGISwapChain& swap_chain)
		{
			const auto extent = get_extent(swap_chain);
			const auto aspect = gsl::narrow<float>(extent.width) / extent.height;
			return DirectX::XMMatrixPerspectiveFovLH(3.141f / 2.0f, aspect, 0.01f, 50.0f);
		}

		void create_backbuffer_view(
			ID3D12Device& device,
			D3D12_CPU_DESCRIPTOR_HANDLE view_handle,
			ID3D12Resource& backbuffer)
		{
			static constexpr D3D12_RENDER_TARGET_VIEW_DESC description {
				.Format {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
				.ViewDimension {D3D12_RTV_DIMENSION_TEXTURE2D},
			};

			device.CreateRenderTargetView(&backbuffer, &description, view_handle);
		}

		// TODO: not all of these resources need to be recreated every time the size changes
		auto create_frame_resources(
			ID3D12Device4& device,
			ID3D12DescriptorHeap& rtv_heap,
			ID3D12DescriptorHeap& dsv_heap,
			IDXGISwapChain& swap_chain)
		{
			const auto render_handle_size = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			const auto depth_handle_size = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			auto render_view_handle = rtv_heap.GetCPUDescriptorHandleForHeapStart();
			auto depth_view_handle = dsv_heap.GetCPUDescriptorHandleForHeapStart();
			std::array<per_frame_resources, 2> frame_resources {};
			for (unsigned int i {}; i < 2; ++i) {
				auto depth_buffer = create_depth_buffer(device, depth_view_handle, get_extent(swap_chain));
				auto backbuffer = winrt::capture<ID3D12Resource>(&swap_chain, &IDXGISwapChain::GetBuffer, i);
				create_backbuffer_view(device, render_view_handle, *backbuffer);

				auto command_allocator = winrt::capture<ID3D12CommandAllocator>(
					&device,
					&ID3D12Device::CreateCommandAllocator,
					D3D12_COMMAND_LIST_TYPE_DIRECT);

				auto command_list = winrt::capture<ID3D12GraphicsCommandList>(
					&device,
					&ID3D12Device4::CreateCommandList1,
					0,
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					D3D12_COMMAND_LIST_FLAG_NONE);

				frame_resources.at(i) = per_frame_resources {
					.allocator {std::move(command_allocator)},
					.command_list {std::move(command_list)},
					.backbuffer_view {render_view_handle},
					.backbuffer {std::move(backbuffer)},
					.depth_buffer_view {depth_view_handle},
					.depth_buffer {std::move(depth_buffer)},
				};

				render_view_handle.ptr += render_handle_size;
				depth_view_handle.ptr += depth_handle_size;
			}

			return frame_resources;
		}

		auto create_object_buffer(ID3D12Device& device, unsigned int size)
		{
			const D3D12_HEAP_PROPERTIES heap_properties {.Type {D3D12_HEAP_TYPE_UPLOAD}};
			const D3D12_RESOURCE_DESC description {
				.Dimension {D3D12_RESOURCE_DIMENSION_BUFFER},
				.Width {size},
				.Height {1},
				.DepthOrArraySize {1},
				.MipLevels {1},
				.SampleDesc {.Count {1}},
				.Layout {D3D12_TEXTURE_LAYOUT_ROW_MAJOR},
			};

			return winrt::capture<ID3D12Resource>(
				&device,
				&ID3D12Device::CreateCommittedResource,
				&heap_properties,
				D3D12_HEAP_FLAG_NONE,
				&description,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr);
		}

		auto map(ID3D12Resource& resource)
		{
			const D3D12_RANGE range {};
			void* pointer;
			winrt::check_hresult(resource.Map(0, &range, &pointer));
			return static_cast<char*>(pointer);
		}

		void unmap(ID3D12Resource& resource)
		{
			const D3D12_RANGE range {};
			resource.Unmap(0, &range);
		}

		auto load_geometry(ID3D12Device& device, gsl::czstring<> name)
		{
			const auto cube_object = load_wavefront(name);
			const auto vertex_count = cube_object.positions.size();
			const auto index_count = cube_object.faces.size() * cube_object.faces.front().size();
			const auto vertex_bytes = vertex_count * sizeof(vector3);
			const auto index_bytes = index_count * sizeof(unsigned int);
			const auto buffer_size = index_bytes + vertex_bytes;
			const auto buffer = create_object_buffer(device, gsl::narrow<unsigned int>(buffer_size));
			const auto data_pointer = map(*buffer);
			std::memcpy(data_pointer, cube_object.faces.data(), index_bytes);
			std::memcpy(std::next(data_pointer, index_bytes), cube_object.positions.data(), vertex_bytes);
			unmap(*buffer);

			return loaded_geometry {
				.buffer {buffer},
				.index_view {
					.BufferLocation {buffer->GetGPUVirtualAddress()},
					.SizeInBytes {gsl::narrow<unsigned int>(index_bytes)},
					.Format {DXGI_FORMAT_R32_UINT},
				},
				.vertex_view {
					.BufferLocation {buffer->GetGPUVirtualAddress() + index_bytes},
					.SizeInBytes {gsl::narrow<unsigned int>(vertex_bytes)},
					.StrideInBytes {sizeof(vector3)},
				},
				.size {gsl::narrow<unsigned int>(index_count)},
			};
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
	m_dsv_heap {create_descriptor_heap(*m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2)},
	m_root_signatures {create_root_signatures(*m_device)},
	m_pipelines {create_pipeline_states(*m_device, m_root_signatures)},
	m_frame_resources {create_frame_resources(*m_device, *m_rtv_heap, *m_dsv_heap, *m_swap_chain)},
	m_fence_current_value {1},
	m_fence {create_fence(*m_device, m_fence_current_value)},
	m_projection_matrix {compute_projection(*m_swap_chain)},
	m_object {load_geometry(*m_device, "bunny.wv")}
{
}

GSL_SUPPRESS(f .6)
matrix::graphics_engine_state::~graphics_engine_state() noexcept { wait_for_idle(); }

void matrix::graphics_engine_state::update(render_mode type, const DirectX::XMMATRIX& view_matrix)
{
	const auto& resources = wait_for_frame();
	auto& allocator = *resources.allocator;
	auto& command_list = *resources.command_list;
	winrt::check_hresult(resources.allocator->Reset());
	switch (type) {
	case render_mode::debug_grid:
		winrt::check_hresult(command_list.Reset(&allocator, m_pipelines.debug_grid_pipeline.get()));
		record_debug_grid_commands(resources, m_root_signatures, view_matrix, m_projection_matrix);
		break;

	case render_mode::object_view:
		winrt::check_hresult(command_list.Reset(&allocator, m_pipelines.object_pipeline.get()));
		record_object_view_commands(resources, m_root_signatures, view_matrix, m_projection_matrix, m_object);
		break;
	}

	winrt::check_hresult(command_list.Close());

	execute_command_lists(*m_queue, command_list);
	present(*m_swap_chain);
	signal_frame_submission();
}

void matrix::graphics_engine_state::signal_size_change()
{
	wait_for_idle();
	m_frame_resources = {};
	resize(*m_swap_chain);
	m_frame_resources = create_frame_resources(*m_device, *m_rtv_heap, *m_dsv_heap, *m_swap_chain);
	m_projection_matrix = compute_projection(*m_swap_chain);
}

void matrix::graphics_engine_state::wait_for_idle()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value)
		_mm_pause();
}

const matrix::per_frame_resources& matrix::graphics_engine_state::wait_for_frame()
{
	while (m_fence->GetCompletedValue() < m_fence_current_value - 1)
		_mm_pause();

	return m_frame_resources.at(m_swap_chain->GetCurrentBackBufferIndex());
}

void matrix::graphics_engine_state::signal_frame_submission()
{
	winrt::check_hresult(m_queue->Signal(m_fence.get(), ++m_fence_current_value));
}
