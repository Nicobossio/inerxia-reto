#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "infrastructure/http/http_client.hpp"

namespace inerxia::infrastructure::http::test {

class FakeHttpClient final : public HttpClient {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit FakeHttpClient(Handler handler) : handler_(std::move(handler)) {}

    HttpResponse send(const HttpRequest& request) override {
        requests_.push_back(request);
        return handler_(request);
    }

    const std::vector<HttpRequest>& requests() const noexcept { return requests_; }
    std::size_t count() const noexcept { return requests_.size(); }

    void set_handler(Handler handler) { handler_ = std::move(handler); }

    static HttpResponse ok(const char* body) { return HttpResponse{200, body}; }
    static HttpResponse ok_empty() { return HttpResponse{200, std::nullopt}; }

private:
    Handler handler_;
    std::vector<HttpRequest> requests_;
};

inline bool request_body_has(const HttpRequest& request, const std::string& needle) {
    return request.body.has_value() &&
           request.body->find(needle) != std::string::npos;
}

}  // namespace inerxia::infrastructure::http::test