#ifndef UTILS_H
#define UTILS_H

#include "json.hpp"
#include <string>

extern std::string stReadFile(const std::string& filename);
extern std::string stReadBinaryFile(const std::string& filename);
extern bool stReadFileChunkBinary(const std::string& filename, long long start, long long length, std::string& st);
extern size_t GetFileSize(const std::string& filename);

template<typename T>
bool assignJSValue(const nlohmann::json& js, const std::string& key, T& value) {
    if (js.contains(key)) {
        value = js[key].get<T>();
        return true;
    }
    return false;
}

#endif /* UTILS_H */
