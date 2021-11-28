#include "pch.h"

#include "wavefront_loader.h"

namespace importer {
	namespace {
		template <char delimiter, typename iterator_type>
		std::string_view get_next_token(iterator_type& iterator, const iterator_type& last) noexcept
		{
			for (; iterator != last && *iterator == delimiter; ++iterator)
				;

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

		vertex convert_vertex(std::string_view vertex_string)
		{
			auto iterator = vertex_string.cbegin();
			const auto stop = vertex_string.cend();
			const auto position = convert_from<unsigned int>(get_next_token<'/'>(iterator, stop)) - 1;
			const auto texture = convert_from<unsigned int>(get_next_token<'/'>(iterator, stop)) - 1;
			const auto normal = convert_from<unsigned int>(get_next_token<'/'>(iterator, stop)) - 1;
			return vertex {.position {position}, .texture {texture}, .normal {normal}};
		}
	}
}

importer::wavefront importer::load_wavefront(gsl::czstring<> name)
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
			faces.emplace_back(convert_vertex(get_next_token<' '>(line_iterator, line_end)));
			faces.emplace_back(convert_vertex(get_next_token<' '>(line_iterator, line_end)));
			faces.emplace_back(convert_vertex(get_next_token<' '>(line_iterator, line_end)));
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
