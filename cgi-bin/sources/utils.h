#ifndef UTILS_H
#define UTILS_H

#include <string>

extern std::string stReadFile(const std::string& filename);
extern std::string stReadBinaryFile(const std::string& filename);
extern bool stReadFileChunkBinary(const std::string& filename, long long start, long long length, std::string& st);
extern size_t GetFileSize(const std::string& filename);

#endif /* UTILS_H */
