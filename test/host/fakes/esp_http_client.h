#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <esp_err.h>

enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST };
struct esp_http_client_config_t {
    const char *url = nullptr;
    int timeout_ms = 0;
    int (*crt_bundle_attach)(void *) = nullptr;
    bool disable_auto_redirect = false;
};
struct HttpReply {
    std::string path;
    int status = 200;
    std::string body;
    int64_t length = -2; // -2: derive from body; chunked IDF exposes length 0.
    bool chunked = false;
    bool complete = true;
    bool readError = false;
    bool openError = false; // includes TLS verification/transport failure
    bool headerError = false;
};
struct HttpCall {
    std::string url;
    std::map<std::string, std::string> headers;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    std::string body;
};
struct FakeHttpClient {
    HttpReply reply;
    HttpCall call;
    size_t read = 0;
};
using esp_http_client_handle_t = FakeHttpClient *;
inline std::deque<HttpReply> httpReplies;
inline std::vector<HttpCall> httpCalls;
inline size_t liveHttpClients = 0;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(cfg->crt_bundle_attach && cfg->disable_auto_redirect);
    assert(cfg->timeout_ms > 0);
    assert(!httpReplies.empty());
    auto *client = new FakeHttpClient{httpReplies.front(), {cfg->url, {}, HTTP_METHOD_GET, {}}, 0};
    httpReplies.pop_front();
    assert(client->call.url.find(client->reply.path) != std::string::npos);
    ++liveHttpClients;
    return client;
}
inline int esp_http_client_cleanup(esp_http_client_handle_t client) {
    httpCalls.push_back(client->call);
    delete client;
    --liveHttpClients;
    return ESP_OK;
}
inline int esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) {
    c->call.headers[key] = value;
    return ESP_OK;
}
inline int esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t method) {
    c->call.method = method;
    return ESP_OK;
}
inline int esp_http_client_open(esp_http_client_handle_t c, int) { return c->reply.openError ? -1 : 0; }
inline int esp_http_client_get_errno(esp_http_client_handle_t) { return 0; }
inline int esp_http_client_write(esp_http_client_handle_t c, const char *body, int size) {
    const int n = std::min(size, 7); // exercise partial request writes
    c->call.body.append(body, n);
    return n;
}
inline int64_t esp_http_client_get_content_length(esp_http_client_handle_t c) {
    if (c->reply.chunked) return 0;
    return c->reply.length == -2 ? c->reply.body.size() : c->reply.length;
}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    return c->reply.headerError ? -1 : esp_http_client_get_content_length(c);
}
inline int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->reply.status; }
inline bool esp_http_client_is_chunked_response(esp_http_client_handle_t c) { return c->reply.chunked; }
inline bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {
    return c->reply.complete && c->read == c->reply.body.size();
}
inline int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int size) {
    if (c->read == c->reply.body.size() && c->reply.readError) return -1;
    const size_t n = std::min<size_t>(size, c->reply.body.size() - c->read);
    std::memcpy(buffer, c->reply.body.data() + c->read, n);
    c->read += n;
    return n;
}
