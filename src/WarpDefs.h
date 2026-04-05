#ifndef WARPDEFS_H
#define WARPDEFS_H

#pragma once

#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <liburing.h>
#include <sys/epoll.h>

typedef int socket_t;
#define SOCKET_ERROR_VALUE -1

#include <ink/ink.hpp>

#define WARP_API

#ifdef USE_EPOLL
#define MAX_EVENTS 8192
#endif

#define TIMERWHELL_TICK_INTERVAL 1000 // 1 sec
#define SESSION_POOL_SIZE 32*1024
#define MIN_REQUEST_SIZE 16

#define HTTP_VERSION "HTTP/1.1"

enum WARP_API Method : u32
{
    GET = 0,
    POST,
    PUT,
    PATCH,
    DELETE,
    HEAD,
    OPTIONS,
    UNKNOWN
};

// HTTP Status Codes
enum WARP_API StatusCode : u32 {
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

using RequestHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

#ifdef USE_IOURING
enum WARP_API OperationType : u8 {
    Read = 0,
    Write
};

// TO manage Connection Lifecycle
enum WARP_API SessionStatus : u8 {
    Active = 0,
    Closing, // Waiting for kernel to return pending SQEs
    Closed     // Ready to be returned to ObjectPool
};

// Bitmask for exact I/O operations in flight in the kernel
enum IoStateFlags : u8 {
    IO_NONE         = 0,
    IO_READING      = 1 << 0, // A recv SQE is in the ring
    IO_WRITING      = 1 << 1, // A send_zc SQE is in the ring
    IO_WAITING_ZC   = 1 << 2, // Waiting for the F_NOTIF CQE from the NIC
};

struct WARP_API IoRequest {
    IoRequest(Session* s, OperationType ot) :
        session(s), optype(ot) {}
    Session* session;
    OperationType optype;
};
#endif

#endif // WARPDEFS_H
