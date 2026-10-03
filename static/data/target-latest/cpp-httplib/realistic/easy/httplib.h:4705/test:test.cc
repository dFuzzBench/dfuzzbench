// NOTE: This file should be saved as UTF-8 w/ BOM
#include <httplib.h>
#include <signal.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#define SERVER_CERT_FILE "./cert.pem"
#define SERVER_CERT2_FILE "./cert2.pem"
#define SERVER_PRIVATE_KEY_FILE "./key.pem"
#define CA_CERT_FILE "./ca-bundle.crt"
#define CLIENT_CA_CERT_FILE "./rootCA.cert.pem"
#define CLIENT_CA_CERT_DIR "."
#define CLIENT_CERT_FILE "./client.cert.pem"
#define CLIENT_PRIVATE_KEY_FILE "./client.key.pem"
#define CLIENT_ENCRYPTED_CERT_FILE "./client_encrypted.cert.pem"
#define CLIENT_ENCRYPTED_PRIVATE_KEY_FILE "./client_encrypted.key.pem"
#define CLIENT_ENCRYPTED_PRIVATE_KEY_PASS "test012!"
#define SERVER_ENCRYPTED_CERT_FILE "./cert_encrypted.pem"
#define SERVER_ENCRYPTED_PRIVATE_KEY_FILE "./key_encrypted.pem"
#define SERVER_ENCRYPTED_PRIVATE_KEY_PASS "test123!"

using namespace std;
using namespace httplib;

const char *HOST = "localhost";
const int PORT = 1234;

const string LONG_QUERY_VALUE = string(25000, '@');
const string LONG_QUERY_URL = "/long-query-value?key=" + LONG_QUERY_VALUE;

const string TOO_LONG_QUERY_VALUE = string(35000, '@');
const string TOO_LONG_QUERY_URL =
    "/too-long-query-value?key=" + TOO_LONG_QUERY_VALUE;

const std::string JSON_DATA = "{\"hello\":\"world\"}";

const string LARGE_DATA = string(1024 * 1024 * 100, '@'); // 100MB

FormData &get_file_value(std::vector<FormData> &items, const char *key) {
  auto it = std::find_if(items.begin(), items.end(), [&](const FormData &file) {
    return file.name == key;
  });
#ifdef CPPHTTPLIB_NO_EXCEPTIONS
  return *it;
#else
  if (it != items.end()) { return *it; }
  throw std::runtime_error("invalid multipart form data name error");
#endif
}

static void read_file(const std::string &path, std::string &out) {
  std::ifstream fs(path, std::ios_base::binary);
  if (!fs) throw std::runtime_error("File not found: " + path);
  fs.seekg(0, std::ios_base::end);
  auto size = fs.tellg();
  fs.seekg(0);
  out.resize(static_cast<size_t>(size));
  fs.read(&out[0], static_cast<std::streamsize>(size));
}

void performance_test(const char *host) {
  auto port = 1234;

  Server svr;

  svr.Get("/benchmark", [&](const Request & /*req*/, Response &res) {
    res.set_content("Benchmark Response", "text/plain");
  });

  auto listen_thread = std::thread([&]() { svr.listen(host, port); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  Client cli(host, port);

  // Warm-up request to establish connection and resolve DNS
  auto warmup_res = cli.Get("/benchmark");
  ASSERT_TRUE(warmup_res); // Ensure server is responding correctly

  // Run multiple trials and collect timings
  const int num_trials = 20;
  std::vector<int64_t> timings;
  timings.reserve(num_trials);

  for (int i = 0; i < num_trials; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto res = cli.Get("/benchmark");
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    // Assertions after timing measurement to avoid overhead
    ASSERT_TRUE(res);
    EXPECT_EQ(StatusCode::OK_200, res->status);

    timings.push_back(elapsed);
  }

  // Calculate 25th percentile (lower quartile)
  std::sort(timings.begin(), timings.end());
  auto p25 = timings[num_trials / 4];

  // Format timings for output
  std::ostringstream timings_str;
  timings_str << "[";
  for (size_t i = 0; i < timings.size(); i++) {
    if (i > 0) timings_str << ", ";
    timings_str << timings[i];
  }
  timings_str << "]";

  // Localhost HTTP GET should be fast even in CI environments
  EXPECT_LE(p25, 5) << "25th percentile performance is too slow: " << p25
                    << "ms (Issue #1777). Timings: " << timings_str.str();
}

TEST(BenchmarkTest, localhost) { performance_test("localhost"); }

TEST(BenchmarkTest, v6) { performance_test("::1"); }

class UnixSocketTest : public ::testing::Test {
protected:
  void TearDown() override { std::remove(pathname_.c_str()); }

  void client_GET(const std::string &addr) {
    httplib::Client cli{addr};
    cli.set_address_family(AF_UNIX);
    ASSERT_TRUE(cli.is_valid());

    const auto &result = cli.Get(pattern_);
    ASSERT_TRUE(result) << "error: " << result.error();

    const auto &resp = result.value();
    EXPECT_EQ(resp.status, StatusCode::OK_200);
    EXPECT_EQ(resp.body, content_);
  }

  const std::string pathname_{"./httplib-server.sock"};
  const std::string pattern_{"/hi"};
  const std::string content_{"Hello World!"};
};

TEST_F(UnixSocketTest, pathname) {
  httplib::Server svr;
  svr.Get(pattern_, [&](const httplib::Request &, httplib::Response &res) {
    res.set_content(content_, "text/plain");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(pathname_, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(pathname_);
}

#if defined(__linux__) ||                                                      \
    /* __APPLE__ */ (defined(SOL_LOCAL) && defined(SO_PEERPID))
TEST_F(UnixSocketTest, PeerPid) {
  httplib::Server svr;
  std::string remote_port_val;
  svr.Get(pattern_, [&](const httplib::Request &req, httplib::Response &res) {
    res.set_content(content_, "text/plain");
    remote_port_val = std::to_string(req.remote_port);
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(pathname_, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(pathname_);
  EXPECT_EQ(std::to_string(getpid()), remote_port_val);
}
#endif

#ifdef __linux__
TEST_F(UnixSocketTest, abstract) {
  constexpr char svr_path[]{"\x00httplib-server.sock"};
  const std::string abstract_addr{svr_path, sizeof(svr_path) - 1};

  httplib::Server svr;
  svr.Get(pattern_, [&](const httplib::Request &, httplib::Response &res) {
    res.set_content(content_, "text/plain");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(abstract_addr, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(abstract_addr);
}
#endif

TEST_F(UnixSocketTest, HostHeaderAutoSet) {
  httplib::Server svr;
  std::string received_host_header;

  svr.Get(pattern_, [&](const httplib::Request &req, httplib::Response &res) {
    // Capture the Host header sent by the client
    auto host_iter = req.headers.find("Host");
    if (host_iter != req.headers.end()) {
      received_host_header = host_iter->second;
    }
    res.set_content(content_, "text/plain");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(pathname_, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  // Test that Host header is automatically set to "localhost" for Unix socket
  // connections
  httplib::Client cli{pathname_};
  cli.set_address_family(AF_UNIX);
  ASSERT_TRUE(cli.is_valid());

  const auto &result = cli.Get(pattern_);
  ASSERT_TRUE(result) << "error: " << result.error();

  const auto &resp = result.value();
  EXPECT_EQ(resp.status, StatusCode::OK_200);
  EXPECT_EQ(resp.body, content_);

  // Verify that Host header was automatically set to "localhost"
  EXPECT_EQ(received_host_header, "localhost");
}

#ifndef _WIN32
TEST(SocketStream, wait_writable_UNIX) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

  const auto asSocketStream = [&](socket_t fd,
                                  std::function<bool(Stream &)> func) {
    return detail::process_client_socket(
        fd, 0, 0, 0, 0, 0, std::chrono::steady_clock::time_point::min(), func);
  };
  asSocketStream(fds[0], [&](Stream &s0) {
    EXPECT_EQ(s0.socket(), fds[0]);
    EXPECT_TRUE(s0.wait_writable());

    EXPECT_EQ(0, close(fds[1]));
    EXPECT_FALSE(s0.wait_writable());

    return true;
  });
  EXPECT_EQ(0, close(fds[0]));
}

TEST(SocketStream, wait_writable_INET) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT + 1);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int disconnected_svr_sock = -1;
  std::thread svr{[&] {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LE(0, s);
    ASSERT_EQ(0, ::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)));
    ASSERT_EQ(0, listen(s, 1));
    ASSERT_LE(0, disconnected_svr_sock = accept(s, nullptr, nullptr));
    ASSERT_EQ(0, close(s));
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::thread cli{[&