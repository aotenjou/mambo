#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

struct CommandPayload {
    bool hasName = false;
    std::string name;
    bool hasCode = false;
    int code = 0;
};

struct HttpRequestHead {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    int contentLength = 0;
    bool expect100Continue = false;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string body = "{}";
};

static inline std::string TrimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(start, end - start);
}

static inline std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static inline std::string EscapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static inline HttpResponse JsonResponse(int status, const std::string& reason, const std::string& jsonBody) {
    HttpResponse response;
    response.status = status;
    response.reason = reason;
    response.body = jsonBody;
    return response;
}

static inline std::string BuildHttpResponse(const HttpResponse& response) {
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " " << response.reason << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << response.body;
    return out.str();
}

static inline bool ParseJsonStringField(const std::string& body, const std::string& field, std::string& value) {
    const std::string key = "\"" + field + "\"";
    size_t pos = body.find(key);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + key.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos >= body.size() || body[pos] != '"') return false;
    pos++;

    std::string parsed;
    while (pos < body.size()) {
        char ch = body[pos++];
        if (ch == '"') {
            value = parsed;
            return true;
        }
        if (ch == '\\' && pos < body.size()) {
            char esc = body[pos++];
            switch (esc) {
                case '"': parsed += '"'; break;
                case '\\': parsed += '\\'; break;
                case '/': parsed += '/'; break;
                case 'n': parsed += '\n'; break;
                case 'r': parsed += '\r'; break;
                case 't': parsed += '\t'; break;
                default: parsed += esc; break;
            }
        } else {
            parsed += ch;
        }
    }
    return false;
}

static inline bool ParseJsonIntField(const std::string& body, const std::string& field, int& value) {
    const std::string key = "\"" + field + "\"";
    size_t pos = body.find(key);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + key.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;

    int base = 10;
    if (pos + 2 <= body.size() && body[pos] == '0' && (body[pos + 1] == 'x' || body[pos + 1] == 'X')) {
        base = 16;
        pos += 2;
    }

    size_t end = pos;
    while (end < body.size() && std::isxdigit(static_cast<unsigned char>(body[end]))) end++;
    if (end == pos) return false;

    char* parseEnd = nullptr;
    std::string number = body.substr(pos, end - pos);
    long parsed = std::strtol(number.c_str(), &parseEnd, base);
    if (!parseEnd || *parseEnd != '\0') return false;
    value = static_cast<int>(parsed);
    return true;
}

static inline bool ParseCommandPayload(const std::string& body, CommandPayload& payload, std::string* error) {
    payload = CommandPayload{};

    std::string name;
    if (ParseJsonStringField(body, "name", name)) {
        payload.hasName = true;
        payload.name = name;
    }

    int code = 0;
    if (ParseJsonIntField(body, "code", code)) {
        payload.hasCode = true;
        payload.code = code;
    }

    if (!payload.hasName && !payload.hasCode) {
        if (error) *error = "Expected JSON body with string field 'name' or numeric field 'code'";
        return false;
    }
    return true;
}

static inline bool ParseHttpRequestHead(const std::string& rawHead, HttpRequestHead& request, std::string* error) {
    request = HttpRequestHead{};
    std::istringstream lines(rawHead);
    std::string line;

    if (!std::getline(lines, line)) {
        if (error) *error = "Missing request line";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream firstLine(line);
    std::string version;
    if (!(firstLine >> request.method >> request.path >> version)) {
        if (error) *error = "Invalid request line";
        return false;
    }

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = ToLowerAscii(TrimAscii(line.substr(0, colon)));
        std::string value = TrimAscii(line.substr(colon + 1));
        request.headers[key] = value;
    }

    auto contentLength = request.headers.find("content-length");
    if (contentLength != request.headers.end()) {
        int parsedLength = std::atoi(contentLength->second.c_str());
        request.contentLength = parsedLength < 0 ? 0 : parsedLength;
    }

    auto expect = request.headers.find("expect");
    if (expect != request.headers.end()) {
        request.expect100Continue = (ToLowerAscii(expect->second).find("100-continue") != std::string::npos);
    }

    return true;
}
