#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include "utils.h"

std::string stReadFile(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
std::string stReadBinaryFile(const std::string& filename) {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool stReadFileChunkBinary(const std::string& filename, long long start, long long length, std::string& st) {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open() || start < 0 || length < 0) {
        return false;
    }

    file.seekg(start, std::ios::beg);
    st.assign((size_t)length, '\0');
    file.read(&st[0], length);
    st.resize((size_t)file.gcount());

    return file.good() || file.eof();
}

size_t GetFileSize(const std::string& filename) {
    struct stat st = {0};
    stat(filename.c_str(), &st);
    return st.st_size;
}