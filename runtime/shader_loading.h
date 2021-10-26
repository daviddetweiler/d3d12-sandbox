#pragma once

#include "pch.h"

namespace d3d12_sandbox {
	std::vector<char> load_compiled_shader(gsl::cwzstring<> name);
}
