#include "pch.h"

int main(int argc, char** argv)
{
	const gsl::span arguments {argv, gsl::narrow_cast<std::size_t>(argc)};
	if (argc < 3) {
		std::cout << "Usage: import <*.obj> <output>\n";
		return 1;
	}
}
