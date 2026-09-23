#pragma once

#include <optional>
#include <string>

namespace inerxia::infrastructure::http {

enum class HttpMethod { Get, Put, Patch, Delete };

struct HttpRequest {
    HttpMethod method;
    std::string path;
    std::optional<std::string> body;
};

struct HttpResponse {
    int status_code = 0;
    std::optional<std::string> body;

    [[nodiscard]] bool ok() const noexcept {
        return status_code >= 200 && status_code < 300;
    }
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    virtual HttpResponse send(const HttpRequest&) = 0;
};

}  // namespace inerxia::infrastructure::http