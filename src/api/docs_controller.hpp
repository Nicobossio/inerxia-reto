#pragma once

#include <httplib.h>

namespace inerxia::api {

// HTTP documentation & operator surface for the REST API:
//   GET /api/openapi.json  -> OpenAPI 3.0 spec (application/json), embedded
//                             into the binary at build time
//   GET /swagger           -> Swagger UI page (loads swagger-ui from CDN and
//                             points it at /api/openapi.json)
//   GET /ui                -> button-driven operator dashboard (embedded, no CDN)
//   GET /                  -> redirect to /ui
// Pure static content serving — no business logic.
class DocsController {
public:
    void register_routes(httplib::Server& server);

private:
    void handle_openapi_spec(const httplib::Request&, httplib::Response&) const;
    void handle_swagger_ui(const httplib::Request&, httplib::Response&) const;
    void handle_dashboard(const httplib::Request&, httplib::Response&) const;
    void handle_root(const httplib::Request&, httplib::Response&) const;
};

}  // namespace inerxia::api