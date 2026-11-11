#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>
namespace planet {
void atomicWriteFile(const std::filesystem::path& path,std::span<const std::byte> bytes);
std::vector<std::byte> readBoundedFile(const std::filesystem::path& path,std::size_t maximumBytes);
}
