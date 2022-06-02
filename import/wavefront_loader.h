#pragma once

#include "pch.h"

#include "../runtime/stream_format.h"

namespace sandbox {
	struct vertex {
		std::size_t position;
		std::size_t texture;
		std::size_t normal;
	};

	struct wavefront {
		std::vector<vector3> positions;
		std::vector<vector3> textures;
		std::vector<vector3> normals;
		std::vector<vertex> faces;
	};

	wavefront load_wavefront(gsl::czstring name);
}
