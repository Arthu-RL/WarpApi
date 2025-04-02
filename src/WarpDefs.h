#ifndef WARPDEFS_H
#define WARPDEFS_H

#pragma once

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define SOCKET_ERROR_VALUE INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#ifdef __linux__
#include <sys/eventfd.h> // For eventfd
#endif
typedef int socket_t;
#define SOCKET_ERROR_VALUE -1
#endif

#include <ink/ink.hpp>
#include <tbb/concurrent_unordered_map.h>

#define WARP_API

#define MAX_EVENTS 1024
#define EPOLL_WAIT_TIMEOUT 1000 // 1 sec

enum WARP_API Method { GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS, UNKNOWN };

// HTTP Status Codes
enum WARP_API StatusCode {
    // 1xx Informational
    http_continue = 100,
    switching_protocols = 101,
    processing = 102,

    // 2xx Success
    ok = 200,
    created = 201,
    accepted = 202,
    non_authoritative_information = 203,
    no_content = 204,
    reset_content = 205,
    partial_content = 206,

    // 3xx Redirection
    multiple_choices = 300,
    moved_permanently = 301,
    found = 302,
    see_other = 303,
    not_modified = 304,
    use_proxy = 305,
    temporary_redirect = 307,
    permanent_redirect = 308,

    // 4xx Client Errors
    bad_request = 400,
    unauthorized = 401,
    payment_required = 402,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    proxy_authentication_required = 407,
    request_timeout = 408,
    conflict = 409,
    gone = 410,
    length_required = 411,
    precondition_failed = 412,
    payload_too_large = 413,
    uri_too_long = 414,
    unsupported_media_type = 415,
    range_not_satisfiable = 416,
    expectation_failed = 417,
    too_many_requests = 429,

    // 5xx Server Errors
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504,
    http_version_not_supported = 505
};

class WARP_API HttpRequest;
class WARP_API HttpResponse;
class WARP_API Session;
class WARP_API EventLoop;
class WARP_API HttpServer;

typedef std::function<void(const HttpRequest&, HttpResponse&)> RequestHandler;
typedef tbb::concurrent_unordered_map<socket_t, std::shared_ptr<Session>> SessionTable;

#endif // WARPDEFS_H
