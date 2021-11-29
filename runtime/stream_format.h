#pragma once

namespace sandbox {
	struct vector3 {
		float x;
		float y;
		float z;
	};

	struct vertex_data {
		vector3 position;
		vector3 texture_coord;
		vector3 normal;
	};
}
