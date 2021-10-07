#pragma once

#include "pch.h"

namespace matrix {
	struct vector3 {
		float x;
		float y;
		float z;
	};

	struct object_face {
		std::array<unsigned int, 3> indices;
	};

	struct wavefront {
		std::vector<vector3> positions;
		std::vector<object_face> faces;
	};

	wavefront load_wavefront(gsl::czstring<> name);
}
