#include "planet/file_io.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace planet {
std::vector<std::byte> readBoundedFile(const std::filesystem::path& path,std::size_t limit) {
    if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>limit) throw std::runtime_error("Invalid file size or type");
    std::ifstream in(path,std::ios::binary|std::ios::ate);const auto size=in.tellg();
    if(!in || size<0 || static_cast<unsigned long long>(size)>limit) throw std::runtime_error("Cannot read file");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!in || in.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Incomplete or changing file");
    return bytes;
}
void atomicWriteFile(const std::filesystem::path& path,std::span<const std::byte> bytes) {
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<unsigned long long> sequence=0;
    auto temporary=path;temporary+=std::filesystem::path(".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
    try {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("Cannot create temporary file");
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));out.flush();out.close();
        if(!out) throw std::runtime_error("Cannot write complete file");
#ifdef _WIN32
        const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open file for durable flush");
        const bool flushed=FlushFileBuffers(handle)!=FALSE;CloseHandle(handle);
        if(!flushed) throw std::runtime_error("Cannot flush saved file");
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace saved file (Windows error "+std::to_string(GetLastError())+")");
#else
        const int fd=open(temporary.c_str(),O_RDONLY);if(fd<0) throw std::runtime_error("Cannot open file for durable flush");
        const int result=fsync(fd);close(fd);if(result!=0) throw std::runtime_error("Cannot flush saved file");
        std::filesystem::rename(temporary,path);
#endif
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
