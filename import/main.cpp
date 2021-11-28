#include "pch.h"

#include "wavefront_loader.h"

namespace importer {
	namespace {
		struct vertex_data {
			vector3 position;
			vector3 texture_coord;
			vector3 normal;
		};
	}
}

int main(int argc, char** argv)
{
	using namespace importer;

	const gsl::span arguments {argv, gsl::narrow_cast<std::size_t>(argc)};
	if (argc < 3) {
		std::cout << "Usage: import <*.obj> <output>\n";
		return 1;
	}

	const auto object = load_wavefront(arguments[1]);
	std::cout << "Found:\n\t" << object.faces.size() << " triangles,\n";
	std::cout << "\t" << object.positions.size() << " vertices\n";
	std::cout << "\t" << object.textures.size() << " textures\n";
	std::cout << "\t" << object.normals.size() << " normals\n";
}
