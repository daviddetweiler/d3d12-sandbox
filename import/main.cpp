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

	bool operator==(const vertex& a, const vertex& b) noexcept
	{
		return a.position == b.position && a.texture == b.texture && a.normal == b.normal;
	}
}

namespace std {
	template <>
	struct hash<importer::vertex> {
		std::size_t operator()(const importer::vertex& v) const noexcept
		{
			std::uint32_t hash = 0xffffffff;
			hash = _mm_crc32_u32(hash, v.position);
			hash = _mm_crc32_u32(hash, v.texture);
			hash = _mm_crc32_u32(hash, v.normal);
			return hash;
		}
	};
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
	std::cout << "Found:\n\t" << object.faces.size() << " vertices,\n";
	std::cout << "\t" << object.positions.size() << " posiitons\n";
	std::cout << "\t" << object.textures.size() << " textures\n";
	std::cout << "\t" << object.normals.size() << " normals\n";

	std::unordered_map<vertex, std::size_t> vertex_set;
	std::vector<vertex_data> vertices;
	std::vector<std::size_t> indices;
	for (const auto& vertex : object.faces) {
		const auto& [iterator, inserted] = vertex_set.insert({vertex, vertices.size()});
		if (inserted) {
			indices.emplace_back(vertices.size());
			vertices.emplace_back(vertex_data {
				.position {object.positions.at(vertex.position)},
				.texture_coord {object.textures.at(vertex.texture)},
				.normal {object.normals.at(vertex.normal)}});
		}
		else {
			indices.push_back(iterator->second);
		}
	}

	std::cout << "Repacked " << indices.size() << " indices and " << vertices.size() << " vertices\n";
}
