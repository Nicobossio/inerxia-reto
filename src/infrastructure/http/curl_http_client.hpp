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

    // Reads MIKROTIK_BASE_URL, MIKROTIK_USER, MIKROTIK_PASSWORD (required);
    // MIKROTIK_CONNECT_TIMEOUT_SECONDS (default 10), MIKROTIK_TIMEOUT_SECONDS (default 30),
    // MIKROTIK_VERIFY_TLS (default true).
    static CurlHttpClientConfig from_env();
};

class CurlHttpClient final : public HttpClient {
public:
    explicit CurlHttpClient(CurlHttpClientConfig);

    HttpResponse send(const HttpRequest&) override;

private:
    CurlHttpClientConfig config_;
};

}  // namespace inerxia::infrastructure::http