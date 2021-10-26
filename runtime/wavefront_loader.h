#pragma once

#include "pch.h"

namespace d3d12_sandbox {
	struct vector3 {
		float x;
		float y;
		float z;
	};

	struct wavefront {
		std::vector<vector3> positions;
		std::vector<std::array<unsigned int, 3>> faces;
	};

	wavefront load_wavefront(gsl::czstring<> name);
}
