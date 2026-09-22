#include <cstring>
#include <fstream>
#include <sstream>
#include <stdarg.h>
#include <sys/stat.h>

#include <sys/un.h>
#include <sys/socket.h>

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
    FILE *fp = fopen(filename.c_str(), "w");
    if (!fp) {
        return false;
    }
    fwrite(content.data(), 1, content.size(), fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

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

int setMachineTimezone(const std::string timezone) {
    if (timezone.empty() ||
        timezone.find("..") != std::string::npos ||
        timezone.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_+-.") != std::string::npos) {
            return -1;
        }

    std::string filename = "/oem/usr/share/zoneinfo/" + timezone;
    if (access(filename.c_str(), F_OK) != 0) {
        return -1;
    }

    wrteFile("/userdata/TZ", timezone);
    int rc = runCommands("ln -sf /oem/usr/share/zoneinfo/%s /etc/localtime", timezone.c_str());
    if (rc == 0) {
        tzset();
    }
    return rc;
}

int runFormatExitDisks(void) {
    /**
     * Because P2P is storage in SD, so we can't format SD in this process.
     * So we send a invoke message to P2P to format SD.
     */
    int fd = -1;
    const char *filename = (const char*)"/tmp/p2p.sock";
    const char *messages = (const char*)"{\"method\":\"SD_FORMAT\",\"params\":{}}";
    
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, filename, sizeof(addr.sun_path) - 1);
    ssize_t written = sendto( fd, messages, strlen(messages), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    close(fd);
    return (written == strlen(messages)) ? 0 : -2;
}