#include "infrastructure/http/curl_http_client.hpp"

#include <curl/curl.h>

#include <string>

#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure::http {
namespace {

std::size_t append_body(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* output = static_cast<std::string*>(userdata);
    output->append(data, size * nmemb);
    return size * nmemb;
}

const char* method_as_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::Get:
            return "GET";
        case HttpMethod::Put:
            return "PUT";
        case HttpMethod::Patch:
            return "PATCH";
        case HttpMethod::Delete:
            return "DELETE";
    }
    return "GET";
}

}  // namespace

CurlHttpClient::CurlHttpClient(CurlHttpClientConfig config) : config_(std::move(config)) {}

HttpResponse CurlHttpClient::send(const HttpRequest& request) {
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        throw InfrastructureError("curl_easy_init failed");
    }

    const std::string url = config_.base_url + "/" + request.path;
    std::string response_body;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_as_string(request.method));
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &append_body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, config_.connect_timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, config_.request_timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_USERPWD,
                     (config_.username + ":" + config_.password).c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    if (!config_.verify_tls) {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (request.body.has_value()) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body->c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body->size()));
    }

    const CURLcode result = curl_easy_perform(handle);
    long status_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    // No credentials or full URL are ever included here: the password only lives in
    // config_ and is neither logged nor part of this exception payload.
    if (result != CURLE_OK) {
        throw InfrastructureError("HTTP request failed: " +
                                  std::string(curl_easy_strerror(result)));
    }

    HttpResponse http_response;
    http_response.status_code = static_cast<int>(status_code);
    if (!response_body.empty()) {
        http_response.body = std::move(response_body);
    }
    return http_response;
}

}  // namespace inerxia::infrastructure::http