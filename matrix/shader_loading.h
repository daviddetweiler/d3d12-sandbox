#pragma once

#include "pch.h"

#include "gsl.h"

namespace matrix {
	std::vector<char> load_compiled_shader(gsl::cwzstring<> name);
}
