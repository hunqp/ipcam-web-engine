#include <cstring>
#include <fstream>
#include <sstream>
#include <stdarg.h>
#include <sys/stat.h>

#include "utils.h"
#include "driver/param/rk_param.h"

std::string readFile(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool wrteFile(const std::string& filename, const std::string& content) {
    std::ofstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }
    file << content;
	system("sync");
    return true;
}

std::string readBin(const std::string& filename) {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

int runCommands(const char *fmt, ...) {
	char cmd[256] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    return system(cmd);
}

std::string runShellCommands(const char *fmt, ...) {
	char cmd[256] = {0};
	std::string result;
	std::array<char, 128> buffer {};
	
	va_list args;
	va_start(args, fmt);
	vsprintf(cmd, fmt, args);
	va_end(args);
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);

	if (!pipe) {
		return std::string("UNKNOWN");
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
	result.erase(result.find_last_not_of("\n") + 1);
	return result;
}

std::string MD5Sum(const std::string& filename) {
    return runShellCommands("md5sum %s | awk '{print $1}'", filename.c_str());
}