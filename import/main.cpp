#include "pch.h"

#include "../runtime/stream_format.h"
#include "wavefront_loader.h"

namespace importer {
	bool operator==(const vertex& a, const vertex& b) noexcept
	{
		return a.position == b.position && a.texture == b.texture && a.normal == b.normal;
	}

	GSL_SUPPRESS(type) // Used to write byte representation to a binary file
	void write_streams(
		gsl::czstring<> filename,
		const std::vector<unsigned int>& indices,
		const std::vector<d3d12_sandbox::vertex_data>& vertices)
	{
		std::ofstream outfile {filename, outfile.binary};
		outfile.exceptions(outfile.failbit | outfile.badbit);
		const auto index_count = indices.size();
		const auto vertex_count = vertices.size();
		outfile.write(reinterpret_cast<const char*>(&index_count), sizeof(index_count));
		outfile.write(reinterpret_cast<const char*>(&vertex_count), sizeof(vertex_count));
		outfile.write(reinterpret_cast<const char*>(indices.data()), index_count * sizeof(unsigned int));
		outfile.write(
			reinterpret_cast<const char*>(vertices.data()),
			vertex_count * sizeof(d3d12_sandbox::vertex_data));
	}

	void write_wavefront(
		gsl::czstring<> filename,
		const std::vector<unsigned int>& indices,
		const std::vector<d3d12_sandbox::vertex_data>& vertices)
	{
		std::ofstream outfile {filename};
		outfile.exceptions(outfile.failbit | outfile.badbit);
		for (const auto& v : vertices)
			outfile << "v " << v.position.x << " " << v.position.y << " " << v.position.z << "\n";

		for (const auto& v : vertices)
			outfile << "vt " << v.texture_coord.x << " " << v.texture_coord.y << " " << v.texture_coord.z << "\n";

		for (const auto& v : vertices)
			outfile << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";

		const auto index_count = gsl::narrow_cast<gsl::index>(indices.size());
		for (gsl::index i {}; i < index_count; i += 3) {
			const auto a = indices.at(i) + 1;
			const auto b = indices.at(i + 1) + 1;
			const auto c = indices.at(i + 2) + 1;
			outfile << "f " << a << "/" << a << "/" << a << " " << b << "/" << b << "/" << b << " " << c << "/" << c
					<< "/" << c << "\n";
		}
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

	std::unordered_map<vertex, unsigned int> index_map;
	std::vector<d3d12_sandbox::vertex_data> vertices;
	std::vector<unsigned int> indices;
	for (const auto& vertex : object.faces) {
		const auto& [iterator, inserted] = index_map.insert({vertex, gsl::narrow_cast<unsigned int>(vertices.size())});
		if (inserted) {
			indices.emplace_back(iterator->second);
			vertices.emplace_back(d3d12_sandbox::vertex_data {
				.position {object.positions.at(vertex.position)},
				.texture_coord {object.textures.at(vertex.texture)},
				.normal {object.normals.at(vertex.normal)}});
		}
		else {
			indices.push_back(iterator->second);
		}
	}

	std::cout << "Repacked " << indices.size() << " indices and " << vertices.size() << " vertices\n";
	write_streams(arguments[2], indices, vertices);
}
