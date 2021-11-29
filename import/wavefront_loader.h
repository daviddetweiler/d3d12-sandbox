#pragma once

#include "pch.h"

#include "../runtime/stream_format.h"

namespace importer {
	struct vertex {
		unsigned int position;
		unsigned int texture;
		unsigned int normal;
	};

	struct wavefront {
		std::vector<d3d12_sandbox::vector3> positions;
		std::vector<d3d12_sandbox::vector3> textures;
		std::vector<d3d12_sandbox::vector3> normals;
		std::vector<vertex> faces;
	};

	wavefront load_wavefront(gsl::czstring<> name);
}
