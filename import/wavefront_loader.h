#pragma once

#include "pch.h"

namespace importer {
	struct vector3 {
		float x;
		float y;
		float z;
	};

	struct vertex {
		unsigned int position;
		unsigned int normal;
	};

	struct triangle {
		static constexpr std::size_t vertex_count = 3;

		vertex a;
		vertex b;
		vertex c;
	};

	struct wavefront {
		std::vector<vector3> positions;
		std::vector<vector3> normals;
		std::vector<triangle> faces;
	};

	wavefront load_wavefront(gsl::czstring<> name);
}
