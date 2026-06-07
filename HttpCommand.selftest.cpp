#include "HttpCommand.h"

#include <cstdlib>
#include <iostream>
#include <string>

static void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

int main() {
    CommandPayload payload;
    std::string error;

    Expect(ParseCommandPayload("{\"name\":\"tail\"}", payload, &error), "name payload parses");
    Expect(payload.hasName && payload.name == "tail", "name value is tail");
    Expect(!payload.hasCode, "name payload does not set code");

    payload = CommandPayload{};
    const std::string prettyJson = "{\r\n    \"name\":  \"upright\"\r\n}";
    Expect(ParseCommandPayload(prettyJson, payload, &error), "PowerShell pretty JSON parses");
    Expect(payload.hasName && payload.name == "upright", "pretty JSON name value is upright");

    payload = CommandPayload{};
    Expect(ParseCommandPayload("{\"code\":58}", payload, &error), "numeric code payload parses");
    Expect(payload.hasCode && payload.code == 0x3A, "numeric code value is 0x3A");

    HttpRequestHead head;
    const std::string rawHead =
        "POST /api/command HTTP/1.1\r\n"
        "Host: 127.0.0.1:5679\r\n"
        "Content-Length: 28\r\n"
        "Expect: 100-continue\r\n"
        "\r\n";
    Expect(ParseHttpRequestHead(rawHead, head, &error), "HTTP request head parses");
    Expect(head.method == "POST", "method is POST");
    Expect(head.path == "/api/command", "path is /api/command");
    Expect(head.contentLength == 28, "content length is parsed");
    Expect(head.expect100Continue, "Expect 100-continue is detected");

    std::cout << "HttpCommand self-test passed" << std::endl;
    return 0;
}
