#include "api/docs_controller.hpp"

#include <string>
#include <string_view>

#include "api/openapi_spec.hpp"
#include "api/ui_dashboard.hpp"

namespace inerxia::api {
namespace {

// Minimal shell page; UI assets are pulled from the Swagger UI CDN at page load
// (browser-side), so the server binary stays lightweight (matches the chosen
// HTTP-framework design decision).
constexpr std::string_view kSwaggerUiPage = R"html(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Inerxia ISP Billing &amp; Service API — Swagger UI</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css">
  <style>
    body { margin: 0; }
    /* keep the topbar off so the page stays self-contained */
    .swagger-ui .topbar { display: none; }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
  <script>
    window.onload = function () {
      window.ui = SwaggerUIBundle({
        url: '/api/openapi.json',
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis
        ],
        layout: 'BaseLayout'
      });
    };
  </script>
</body>
</html>
)html";

}  // namespace

void DocsController::register_routes(httplib::Server& server) {
    server.Get("/api/openapi.json", [this](const httplib::Request& req, httplib::Response& res) {
        handle_openapi_spec(req, res);
    });
    server.Get("/swagger", [this](const httplib::Request& req, httplib::Response& res) {
        handle_swagger_ui(req, res);
    });
    // The button-driven operator dashboard is the landing surface; Swagger stays
    // available for the raw OpenAPI walkthrough.
    server.Get("/ui", [this](const httplib::Request& req, httplib::Response& res) {
        handle_dashboard(req, res);
    });
    server.Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        handle_root(req, res);
    });
}

void DocsController::handle_openapi_spec(const httplib::Request&, httplib::Response& response) const {
    response.status = 200;
    response.set_content(std::string{kOpenApiSpec}, "application/json; charset=utf-8");
}

void DocsController::handle_swagger_ui(const httplib::Request&, httplib::Response& response) const {
    response.status = 200;
    response.set_content(std::string{kSwaggerUiPage}, "text/html; charset=utf-8");
}

void DocsController::handle_dashboard(const httplib::Request&, httplib::Response& response) const {
    response.status = 200;
    response.set_content(std::string{kUiDashboard}, "text/html; charset=utf-8");
}

void DocsController::handle_root(const httplib::Request&, httplib::Response& response) const {
    response.status = 302;
    response.set_header("Location", "/ui");
}

}  // namespace inerxia::api