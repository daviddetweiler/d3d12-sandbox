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
		unsigned int texture;
		unsigned int normal;
	};

	struct wavefront {
		std::vector<vector3> positions;
		std::vector<vector3> textures;
		std::vector<vector3> normals;
		std::vector<vertex> faces;
	};

	wavefront load_wavefront(gsl::czstring<> name);
}
