module;

#include <filesystem>
#include <fstream>

export module Aegis.Utils.File;

export namespace Aegis::Utils::File
{
	/// @brief Reads a binary file and returns the contents as a vector of chars
	/// @param filePath Path to the file to read
	/// @return A vector of chars with the contents of the file or an empty vector if the file could not be read
	auto readBinary(const std::filesystem::path& filePath) -> std::vector<char>
	{
		std::ifstream file{ filePath, std::ios::ate | std::ios::binary };
		if (!file.is_open())
			return {};

		auto fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();
		return buffer;
	}

	/// @brief Reads a binary file and returns the contents as a vector of chars
	/// @param filePath Path to the file to read
	/// @param size Number of bytes to read
	/// @param offset Byte offset from the beginning of the file
	/// @return A vector with the contents of the file or an empty vector if the file could not be read
	auto readBinary(const std::filesystem::path& filePath, size_t size, size_t offset = 0) -> std::vector<char>
	{
		std::ifstream file{ filePath, std::ios::binary };
		if (!file.is_open())
			return {};

		file.seekg(offset);
		std::vector<char> buffer(size);
		file.read(buffer.data(), size);
		file.close();

		return buffer;
	}
}
