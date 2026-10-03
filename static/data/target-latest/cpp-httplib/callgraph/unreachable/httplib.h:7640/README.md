cpp-httplib
===========

[![](https://github.com/yhirose/cpp-httplib/workflows/test/badge.svg)](https://github.com/yhirose/cpp-httplib/actions)

A C++11 single-file header-only cross platform HTTP/HTTPS library.

It's extremely easy to set up. Just include the **httplib.h** file in your code!

> [!IMPORTANT]
> This library uses 'blocking' socket I/O. If you are looking for a library with 'non-blocking' socket I/O, this is not the one that you want.

Simple examples
---------------

#### Server (Multi-threaded)

```c++
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "path/to/httplib.h"

// HTTP
httplib::Server svr;

// HTTPS
httplib::SSLServer svr;

svr.Get("/hi", [](const httplib::Request &, httplib::Response &res) {
  res.set_content("Hello World!", "text/plain");
});

svr.listen("0.0.0.0", 8080);
```

#### Client

```c++
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "path/to/httplib.h"

// HTTP
httplib::Client cli("http://yhirose.github.io");

// HTTPS
httplib::Client cli("https://yhirose.github.io");

auto res = cli.Get("/hi");
res->status;
res->body;
```

SSL Support
-----------

SSL support is available with `CPPHTTPLIB_OPENSSL_SUPPORT`. `libssl` and `libcrypto` should be linked.

> [!NOTE]
> cpp-httplib currently supports only version 3.0 or later. Please see [this page](https://www.openssl.org/policies/releasestrat.html) to get more information.

> [!TIP]
> For macOS: cpp-httplib now can use system certs with `CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN`. `CoreFoundation` and `Security` should be linked with `-framework`.

```c++
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "path/to/httplib.h"

// Server
httplib::SSLServer svr("./cert.pem", "./key.pem");

// Client
httplib::Client cli("https://localhost:1234"); // scheme + host
httplib::SSLClient cli("localhost:1234"); // host
httplib::SSLClient cli("localhost", 1234); // host, port

// Use your CA bundle
cli.set_ca_cert_path("./ca-bundle.crt");

// Disable cert verification
cli.enable_server_certificate_verification(false);

// Disable host verification
cli.enable_server_hostname_verification(false);
```

> [!NOTE]
> When using SSL, it seems impossible to avoid SIGPIPE in all cases, since on some operating systems, SIGPIPE can only be suppressed on a per-message basis, but there is no way to make the OpenSSL library do so for its internal communications. If your program needs to avoid being terminated on SIGPIPE, the only fully general way might be to set up a signal handler for SIGPIPE to handle or ignore it yourself.

### SSL Error Handling

When SSL operations fail, cpp-httplib provides detailed error information through two separate error fields:

```c++
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "path/to/httplib.h"

httplib::Client cli("https://example.com");

auto res = cli.Get("/");
if (!res) {
  // Check the error type
  const auto err = res.error();

  switch (err) {
    case httplib::Error::SSLConnection:
      std::cout << "SSL connection failed, SSL error: "
                << res.ssl_error() << std::endl;
      break;

    case httplib::Error::SSLLoadingCerts:
      std::cout << "SSL cert loading failed, OpenSSL error: "
                << std::hex << res.ssl_openssl_error() << std::endl;
      break;

    case httplib::Error::SSLServerVerification:
      std::cout << "SSL verification failed, X509 error: "
                << res.ssl_openssl_error() << std::endl;
      break;

    case httplib::Error::SSLServerHostnameVerification:
      std::cout << "SSL hostname verification failed, X509 error: "
                << res.ssl_openssl_error() << std::endl;
      break;

    default:
      std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
  }
}
```

Server
------

```c++
#include <httplib.h>

int main(void)
{
  using namespace httplib;

  Server svr;

  svr.Get("/hi", [](const Request& req, Response& res) {
    res.set_content("Hello World!", "text/plain");
  });

  // Match the request path against a regular expression
  // and extract its captures
  svr.Get(R"(/numbers/(\d+))", [&](const Request& req, Response& res) {
    auto numbers = req.matches[1];
    res.set_content(numbers, "text/plain");
  });

  // Capture the second segment of the request path as "id" path param
  svr.Get("/users/:id", [&](const Request& req, Response& res) {
    auto user_id = req.path_params.at("id");
    res.set_content(user_id, "text/plain");
  });

  // Extract values from HTTP headers and URL query params
  svr.Get("/body-header-param", [](const Request& req, Response& res) {
    if (req.has_header("Content-Length")) {
      auto val = req.get_header_value("Content-Length");
    }
    if (req.has_param("key")) {
      auto val = req.get_param_value("key");
    }
    res.set_content(req.body, "text/plain");
  });

  // If the handler takes time to finish, you can also poll the connection state
  svr.Get("/task", [&](const Request& req, Response& res) {
    const char * result = nullptr;
    process.run(); // for example, starting an external process
    while (result == nullptr) {
      sleep(1);
      if (req.is_connection_closed()) {
        process.kill(); // kill the process
        return;
      }
      result = process.stdout(); // != nullptr if the process finishes
    }
    res.set_content(result, "text/plain");
  });

  svr.Get("/stop", [&](const Request& req, Response& res) {
    svr.stop();
  });

  svr.listen("localhost", 1234);
}
```

`Post`, `Put`, `Delete` and `Options` methods are also supported.

### Bind a socket to multiple interfaces and any available port

```cpp
int port = svr.bind_to_any_port("0.0.0.0");
svr.listen_after_bind();
```

### Static File Server

```cpp
// Mount / to ./www directory
auto ret = svr.set_mount_point("/", "./www");
if (!ret) {
  // The specified base directory doesn't exist...
}

// Mount /public to ./www directory
ret = svr.set_mount_point("/public", "./www");

// Mount /public to ./www1 and ./www2 directories
ret = svr.set_mount_point("/public", "./www1"); // 1st order to search
ret = svr.set_mount_point("/public", "./www2"); // 2nd order to search

// Remove mount /
ret = svr.remove_mount_point("/");

// Remove mount /public
ret = svr.remove_mount_point("/public");
```

```cpp
// User defined file extension and MIME type mappings
svr.set_file_extension_and_mimetype_mapping("cc", "text/x-c");
svr.set_file_extension_and_mimetype_mapping("cpp", "text/x-c");
svr.set_file_extension_and_mimetype_mapping("hh", "text/x-h");
```

The following are built-in mappings:

| Extension  |          MIME Type          | Extension  |          MIME Type          |
| :--------- | :-------------------------- | :--------- | :-------------------------- |
| css        | text/css                    | mpga       | audio/mpeg                  |
| csv        | text/csv                    | weba       | audio/webm                  |
| txt        | text/plain                  | wav        | audio/wave                  |
| vtt        | text/vtt                    | otf        | font/otf                    |
| html, htm  | text/html                   | ttf        | font/ttf                    |
| apng       | image/apng                  | woff       | font/woff                   |
| avif       | image/avif                  | woff2      | font/woff2                  |
| bmp        | image/bmp                   | 7z         | application/x-7z-compressed |
| gif        | image/gif                   | atom       | application/atom+xml        |
| png        | image/png                   | pdf        | application/pdf             |
| svg        | image/svg+xml               | mjs, js    | text/javascript             |
| webp       | image/webp                  | json       | application/json            |
| ico        | image/x-icon                | rss        | application/rss+xml         |
| tif        | image/tiff                  | tar        | application/x-tar           |
| tiff       | image/tiff                  | xhtml, xht | application/xhtml+xml       |
| jpeg, jpg  | image/jpeg                  | xslt       | application/xslt+xml        |
| mp4        | video/mp4                   | xml        | application/xml             |
| mpeg       | video/mpeg                  | gz         | application/gzip            |
| webm       | video/webm                  | zip        | application/zip             |
| mp3        | audio/mp3                   | wasm       | application/wasm            |

> [!WARNING]
> These static file server methods are not thread-safe.

### File request handler

```cpp
// The handler is called right before the response is sent to a client
svr.set_file_request_handler([](const Request &req, Response &res) {
  ...
});
```

### Logging

cpp-httplib provides separate logging capabilities for access logs and error logs, similar to web servers like Nginx and Apache.

#### Access Logging

Access loggers capture successful HTTP requests and responses:

```cpp
svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
  std::cout << req.method << " " << req.path << " -> " << res.status << std::endl;
});
```

#### Pre-compression Logging

You can also set a pre-compression logger to capture request/response data before compression is applied:

```cpp
svr.set_pre_compression_logger([](const httplib::Request& req, const httplib::Response& res) {
  // Log before compression - res.body contains uncompressed content
  // Content-Encoding header is not yet set
  your_pre_compression_logger(req, res);
});
```

The pre-compression logger is only called when compression would be applied. For responses without compression, only the access logger is called.

#### Error Logging

Error loggers capture failed requests and connection issues. Unlike access loggers, error loggers only receive the Error and Request information, as errors typically occur before a meaningful Response can be generated.

```cpp
svr.set_error_logger([](const httplib::Error& err, const httplib::Request* req) {
  std::cerr << httplib::to_string(err) << " while processing request";
  if (req) {
    std::cerr << ", client
