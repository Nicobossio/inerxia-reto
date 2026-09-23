#pragma once

#include <string>

#include "infrastructure/http/http_client.hpp"

namespace inerxia::infrastructure::http {

struct CurlHttpClientConfig {
    std::string base_url;
    std::string username;
    std::string password;
    int connect_timeout_seconds = 10;
    int request_timeout_seconds = 30;
    bool verify_tls = true;
};

class CurlHttpClient final : public HttpClient {
public:
    explicit CurlHttpClient(CurlHttpClientConfig);

    HttpResponse send(const HttpRequest&) override;

private:
    CurlHttpClientConfig config_;
};

}  // namespace inerxia::infrastructure::http