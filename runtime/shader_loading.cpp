#include "pch.h"

#include "shader_loading.h"

namespace sandbox {
	namespace {
		auto get_self_path()
		{
			std::vector<wchar_t> path_buffer(MAX_PATH + 1);
			winrt::check_bool(GetModuleFileName(nullptr, path_buffer.data(), MAX_PATH + 1));
			return std::filesystem::path {path_buffer.data()}.parent_path();
		}
	}
}

std::vector<char> sandbox::load_compiled_shader(gsl::cwzstring name)
{
	static const auto parent_path {get_self_path()};
	const auto path = parent_path / name;
	std::vector<char> buffer(std::filesystem::file_size(path));
	std::ifstream reader {path, reader.binary};
	reader.exceptions(reader.badbit | reader.failbit);
	reader.read(buffer.data(), buffer.size());
	return buffer;
}
