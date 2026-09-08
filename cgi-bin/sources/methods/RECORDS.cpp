/*
    @Records download

    "/records/<date>/<clip>.mp4" used to be a plain lighttpd alias onto
    "/mnt/sdcard/", i.e. the whole recording tree was reachable over HTTP with no
    authentication. It is now routed through FastCGI: main() validates the session
    and then calls HTTP_ServeRecordFile(), which authorises the path and hands the
    actual transfer back to lighttpd via "X-Sendfile:".
*/
#include <climits>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>

#include "main.h"
#include "http_utils.h"

#define RECORDS_ROOT "/mnt/sdcard"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
    Map a request path ("/records/2025-09-08/clip.mp4") to a canonical file path
    under RECORDS_ROOT. Returns false (caller answers 404) for anything that is
    missing, not a regular file, or that resolves outside the recordings root.
*/
static bool recordsResolvePath(const std::string &urlPath, std::string &outFsPath) {
    if (urlPath.compare(0, 9, "/records/") != 0) {
        return false;
    }
    const std::string rel = urlPath.substr(9);

    /* Percent-decode */
    std::string decoded;
    decoded.reserve(rel.size());
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < rel.size(); ++i) {
        if (rel[i] == '%' && i + 2 < rel.size() &&
            hexval(rel[i + 1]) >= 0 && hexval(rel[i + 2]) >= 0) {
            decoded += static_cast<char>((hexval(rel[i + 1]) << 4) | hexval(rel[i + 2]));
            i += 2;
        } else {
            decoded += rel[i];
        }
    }

    if (decoded.empty() || decoded.size() > 255 || decoded.front() == '/') {
        return false;
    }

    /**
     * Reject traversal and control bytes only. Recording file names carry
     * timestamps that legitimately contain ':' / '.' / '-' / '_' / spaces, so a
     * strict character whitelist would break normal playback. The realpath()
     * containment check below is the actual security boundary.
     */
    size_t start = 0;
    for (;;) {
        size_t slash = decoded.find('/', start);
        const std::string seg = decoded.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (seg.empty() || seg == "." || seg == "..") {
            return false;
        }
        for (unsigned char c : seg) {
            if (c < 0x20 || c == 0x7f || c == '\\') {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    /* Canonical recordings root (survives /mnt/sdcard being a symlink/bind mount) */
    char rootReal[PATH_MAX] = {0};
    if (!realpath(RECORDS_ROOT, rootReal)) {
        return false;
    }
    const std::string rootPrefix = std::string(rootReal) + "/";

    const std::string candidate = rootPrefix + decoded;

    /* Canonicalise the target and make sure it never escaped the root */
    char resolved[PATH_MAX] = {0};
    if (!realpath(candidate.c_str(), resolved)) {
        return false;
    }
    if (strncmp(resolved, rootPrefix.c_str(), rootPrefix.size()) != 0) {
        return false;
    }

    struct stat st = {0};
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    outFsPath.assign(resolved);
    return true;
}

static const char *recordsContentType(const std::string &path) {
    auto endsWith = [&](const char *ext) {
        const size_t n = strlen(ext);
        return path.size() >= n && strcasecmp(path.c_str() + path.size() - n, ext) == 0;
    };
    if (endsWith(".mp4"))  return "video/mp4";
    if (endsWith(".flv"))  return "video/x-flv";
    if (endsWith(".jpeg") || endsWith(".jpg")) return "image/jpeg";
    if (endsWith(".png"))  return "image/png";
    return "application/octet-stream";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void HTTP_ServeRecordFile(FCGX_Request &message, const std::string &urlPath) {
    std::string fsPath;
    if (!recordsResolvePath(urlPath, fsPath)) {
        CGI_SYSW("Record download rejected for '%s'\r\n", urlPath.c_str());
        HTTP_ResponseDataAsHTML(message, 404, "<h1>404</h1>");
        return;
    }
    CGI_SYSD("Record download: '%s' -> X-Sendfile '%s'\r\n", urlPath.c_str(), fsPath.c_str());

    /**
     * Hand the actual transfer to lighttpd via X-Sendfile:
     *   - the single FastCGI worker (max-procs = 1) must NOT block for the whole
     *     duration of a multi-MB clip, or it can serve nothing else meanwhile;
     *   - lighttpd does sendfile(), byte ranges, Content-Type and parallel
     *     connections natively.
     * Requires '"allow-x-send-file" => "enable"' on the /records fastcgi.server
     * block. lighttpd re-opens/fstats the file and fills in Content-Length /
     * Content-Range / 206 itself, so we only emit a tiny header block here
     * (note: the bundled FCGX_FPrintF cannot format %lld anyway).
     */
    FCGX_FPrintF(message.out, "Status: 200 OK\r\n");
    FCGX_FPrintF(message.out, "Content-Type: %s\r\n", recordsContentType(fsPath));
    FCGX_FPrintF(message.out, "Cache-Control: no-store\r\n");
    FCGX_FPrintF(message.out, "X-Sendfile: %s\r\n", fsPath.c_str());
    FCGX_FPrintF(message.out, "\r\n");
}
