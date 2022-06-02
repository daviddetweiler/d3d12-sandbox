#include "pch.h"

#include "wavefront_loader.h"

namespace sandbox {
	namespace {
		template <char delimiter, bool skip_leading = true, typename iterator_type>
		std::string_view get_next_token(iterator_type& iterator, const iterator_type& last) noexcept
		{
			if constexpr (skip_leading) {
				for (; iterator != last && *iterator == delimiter; ++iterator)
					;
			}

			const iterator_type first_char {iterator};
			for (; iterator != last && *iterator != delimiter; ++iterator)
				;

			const iterator_type last_char {iterator};

			if (first_char == last_char)
				return {};

			return {&*first_char, gsl::narrow_cast<std::size_t>(last_char - first_char)};
		}

		template <typename type>
		type convert_from(std::string_view string)
		{
			type value {};
			std::from_chars(string.data(), std::next(string.data(), string.size()), value);
			return value;
		}

		std::size_t map_index(std::ptrdiff_t n, std::size_t size) noexcept
		{
			if (n < 0)
				return size + n;
			else if (n > 0)
				return n - 1;
			else
				return std::numeric_limits<std::size_t>::max();
		}

		vertex convert_vertex(
			std::string_view vertex_string,
			std::size_t n_positions,
			std::size_t n_textures,
			std::size_t n_normals)
		{
			auto iterator = vertex_string.cbegin();
			const auto stop = vertex_string.cend();
			const auto position = convert_from<std::ptrdiff_t>(get_next_token<'/', false>(iterator, stop));
			++iterator;
			const auto texture = convert_from<std::ptrdiff_t>(get_next_token<'/', false>(iterator, stop));
			++iterator;
			const auto normal = convert_from<std::ptrdiff_t>(get_next_token<'/', false>(iterator, stop));
			return vertex {
				.position {map_index(position, n_positions)},
				.texture {map_index(texture, n_textures)},
				.normal {map_index(normal, n_normals)}};
		}
	}
}

sandbox::wavefront sandbox::load_wavefront(gsl::czstring name)
{
	std::ifstream object_file {name, object_file.ate | object_file.binary};
	object_file.exceptions(object_file.badbit | object_file.failbit);

	std::vector<char> content(object_file.tellg());
	object_file.seekg(object_file.beg);
	object_file.read(content.data(), content.size());

	auto content_iterator = content.begin();
	const auto content_end = content.end();

	auto non_triangles = 0;
	std::vector<vector3> positions {};
	std::vector<vertex> faces {};
	std::vector<vector3> normals {};
	std::vector<vector3> textures {};
	while (true) {
		const auto next_line = get_next_token<'\r'>(content_iterator, content_end);
		if (next_line.empty())
			break;

		const auto line_end = next_line.end();
		auto line_iterator = ++next_line.begin();
		const auto line_type = get_next_token<' '>(line_iterator, line_end);
		if (line_type == "v") {
			const auto x = get_next_token<' '>(line_iterator, line_end);
			const auto y = get_next_token<' '>(line_iterator, line_end);
			const auto z = get_next_token<' '>(line_iterator, line_end);
			positions.push_back({convert_from<float>(x), convert_from<float>(y), convert_from<float>(z)});
		}
		else if (line_type == "f") {
			faces.emplace_back(convert_vertex(
				get_next_token<' '>(line_iterator, line_end),
				positions.size(),
				textures.size(),
				normals.size()));

			faces.emplace_back(convert_vertex(
				get_next_token<' '>(line_iterator, line_end),
				positions.size(),
				textures.size(),
				normals.size()));

			faces.emplace_back(convert_vertex(
				get_next_token<' '>(line_iterator, line_end),
				positions.size(),
				textures.size(),
				normals.size()));

			if (!get_next_token<' '>(line_iterator, line_end).empty())
				++non_triangles;
		}
		else if (line_type == "vn") {
			const auto x = get_next_token<' '>(line_iterator, line_end);
			const auto y = get_next_token<' '>(line_iterator, line_end);
			const auto z = get_next_token<' '>(line_iterator, line_end);
			normals.push_back({convert_from<float>(x), convert_from<float>(y), convert_from<float>(z)});
		}
		else if (line_type == "vt") {
			const auto x = get_next_token<' '>(line_iterator, line_end);
			const auto y = get_next_token<' '>(line_iterator, line_end);
			const auto z = get_next_token<' '>(line_iterator, line_end);
			textures.push_back({convert_from<float>(x), convert_from<float>(y), convert_from<float>(z)});
		}
	}

	if (non_triangles)
		std::cout << "warning: " << non_triangles << " faces were not triangles\n";


	return {
		.positions {std::move(positions)},
		.textures {std::move(textures)},
		.normals {std::move(normals)},
		.faces {std::move(faces)}};
}
