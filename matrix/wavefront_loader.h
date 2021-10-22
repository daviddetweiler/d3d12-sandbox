#pragma once

#include "pch.h"

#include "gsl.h"

namespace matrix {
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
