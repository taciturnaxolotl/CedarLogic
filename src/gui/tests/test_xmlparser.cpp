#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "XMLParser.h"

#include <filesystem>
#include <fstream>

TEST_CASE("the obsolete XML reader stops cleanly at EOF") {
	const auto path = std::filesystem::temp_directory_path() / "cedarlogic-xmlparser-eof.cdl";
	{
		std::ofstream out(path, std::ios::binary);
		out << "(cedarlogic (version 3))";
	}

	std::fstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	XMLParser parser(&in, false);
	std::filesystem::remove(path);
}
