// Copyright 2005, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//
// The Google C++ Testing and Mocking Framework (Google Test)

#include "gtest/gtest.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include <algorithm>
#include <chrono>  // NOLINT
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <ostream>  // NOLINT
#include <sstream>
#include <unordered_set>
#include <vector>

#include "gtest/gtest-assertion-result.h"
#include "gtest/gtest-spi.h"
#include "gtest/internal/custom/gtest.h"

#if GTEST_OS_LINUX

#include <fcntl.h>   // NOLINT
#include <limits.h>  // NOLINT
#include <sched.h>   // NOLINT
// Declares vsnprintf().  This header is not available on Windows.
#include <strings.h>   // NOLINT
#include <sys/mman.h>  // NOLINT
#include <sys/time.h>  // NOLINT
#include <unistd.h>    // NOLINT

#include <string>

#elif GTEST_OS_ZOS
#include <sys/time.h>  // NOLINT

// On z/OS we additionally need strings.h for strcasecmp.
#include <strings.h>   // NOLINT

#elif GTEST_OS_WINDOWS_MOBILE  // We are on Windows CE.

#include <windows.h>  // NOLINT
#undef min

#elif GTEST_OS_WINDOWS  // We are on Windows proper.

#include <windows.h>  // NOLINT
#undef min

#ifdef _MSC_VER
#include <crtdbg.h>  // NOLINT
#endif

#include <io.h>         // NOLINT
#include <sys/stat.h>   // NOLINT
#include <sys/timeb.h>  // NOLINT
#include <sys/types.h>  // NOLINT

#if GTEST_OS_WINDOWS_MINGW
#include <sys/time.h>  // NOLINT
#endif                 // GTEST_OS_WINDOWS_MINGW

#else

// cpplint thinks that the header is already included, so we want to
// silence it.
#include <sys/time.h>  // NOLINT
#include <unistd.h>    // NOLINT

#endif  // GTEST_OS_LINUX

#if GTEST_HAS_EXCEPTIONS
#include <stdexcept>
#endif

#if GTEST_CAN_STREAM_RESULTS_
#include <arpa/inet.h>   // NOLINT
#include <netdb.h>       // NOLINT
#include <sys/socket.h>  // NOLINT
#include <sys/types.h>   // NOLINT
#endif

#include "src/gtest-internal-inl.h"

#if GTEST_OS_WINDOWS
#define vsnprintf _vsnprintf
#endif  // GTEST_OS_WINDOWS

#if GTEST_OS_MAC
#ifndef GTEST_OS_IOS
#include <crt_externs.h>
#endif
#endif

#if GTEST_HAS_ABSL
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#endif  // GTEST_HAS_ABSL

namespace testing {

using internal::CountIf;
using internal::ForEach;
using internal::GetElementOr;
using internal::Shuffle;

// Constants.

// A test whose test suite name or test name matches this filter is
// disabled and not run.
static const char kDisableTestFilter[] = "DISABLED_*:*/DISABLED_*";

// A test suite whose name matches this filter is considered a death
// test suite and will be run before test suites whose name doesn't
// match this filter.
static const char kDeathTestSuiteFilter[] = "*DeathTest:*DeathTest/*";

// A test filter that matches everything.
static const char kUniversalFilter[] = "*";

// The default output format.
static const char kDefaultOutputFormat[] = "xml";
// The default output file.
static const char kDefaultOutputFile[] = "test_detail";

// The environment variable name for the test shard index.
static const char kTestShardIndex[] = "GTEST_SHARD_INDEX";
// The environment variable name for the total number of test shards.
static const char kTestTotalShards[] = "GTEST_TOTAL_SHARDS";
// The environment variable name for the test shard status file.
static const char kTestShardStatusFile[] = "GTEST_SHARD_STATUS_FILE";

namespace internal {

// The text used in failure messages to indicate the start of the
// stack trace.
const char kStackTraceMarker[] = "\nStack trace:\n";

// g_help_flag is true if and only if the --help flag or an equivalent form
// is specified on the command line.
bool g_help_flag = false;

// Utility function to Open File for Writing
static FILE* OpenFileForWriting(const std::string& output_file) {
  FILE* fileout = nullptr;
  FilePath output_file_path(output_file);
  FilePath output_dir(output_file_path.RemoveFileName());

  if (output_dir.CreateDirectoriesRecursively()) {
    fileout = posix::FOpen(output_file.c_str(), "w");
  }
  if (fileout == nullptr) {
    GTEST_LOG_(FATAL) << "Unable to open file \"" << output_file << "\"";
  }
  return fileout;
}

}  // namespace internal

// Bazel passes in the argument to '--test_filter' via the TESTBRIDGE_TEST_ONLY
// environment variable.
static const char* GetDefaultFilter() {
  const char* const testbridge_test_only =
      internal::posix::GetEnv("TESTBRIDGE_TEST_ONLY");
  if (testbridge_test_only != nullptr) {
    return testbridge_test_only;
  }
  return kUniversalFilter;
}

// Bazel passes in the argument to '--test_runner_fail_fast' via the
// TESTBRIDGE_TEST_RUNNER_FAIL_FAST environment variable.
static bool GetDefaultFailFast() {
  const char* const testbridge_test_runner_fail_fast =
      internal::posix::GetEnv("TESTBRIDGE_TEST_RUNNER_FAIL_FAST");
  if (testbridge_test_runner_fail_fast != nullptr) {
    return strcmp(testbridge_test_runner_fail_fast, "1") == 0;
  }
  return false;
}

}  // namespace testing

GTEST_DEFINE_bool_(
    fail_fast,
    testing::internal::BoolFromGTestEnv("fail_fast",
                                        testing::GetDefaultFailFast()),
    "True if and only if a test failure should stop further test execution.");

GTEST_DEFINE_bool_(
    also_run_disabled_tests,
    testing::internal::BoolFromGTestEnv("also_run_disabled_tests", false),
    "Run disabled tests too, in addition to the tests normally being run.");

GTEST_DEFINE_bool_(
    break_on_failure,
    testing::internal::BoolFromGTestEnv("break_on_failure", false),
    "True if and only if a failed assertion should be a debugger "
    "break-point.");

GTEST_DEFINE_bool_(catch_exceptions,
                   testing::internal::BoolFromGTestEnv("catch_exceptions",
                                                       true),
                   "True if and only if " GTEST_NAME_
                   " should catch exceptions and treat them as test failures.");

GTEST_DEFINE_string_(
    color, testing::internal::StringFromGTestEnv("color", "auto"),
    "Whether to use colors in the output.  Valid values: yes, no, "
    "and auto.  'auto' means to use colors if the output is "
    "being sent to a terminal and the TERM environment variable "
    "is set to a terminal type that supports colors.");

GTEST_DEFINE_string_(
    filter,
    testing::internal::StringFromGTestEnv("filter",
                                          testing::GetDefaultFilter()),
    "A colon-separated list of glob (not regex) patterns "
    "for filtering the tests to run, optionally followed by a "
    "'-' and a : separated list of negative patterns (tests to "
    "exclude).  A test is run if it matches one of the positive "
    "patterns and does not match any of the negative patterns.");

GTEST_DEFINE_bool_(
    install_failure_signal_handler,
    testing::internal::BoolFromGTestEnv("install_failure_signal_handler",
                                        false),
    "If true and supported on the current platform, " GTEST_NAME_
    " should "
    "install a signal handler that dumps debugging information when fatal "
    "signals are raised.");

GTEST_DEFINE_bool_(list_tests, false, "List all tests without running them.");

// The net priority order after flag processing is thus:
//   --gtest_output command line flag
//   GTEST_OUTPUT environment variable
//   XML_OUTPUT_FILE environment variable
//   ''
GTEST_DEFINE_string_(
    output,
    testing::internal::StringFromGTestEnv(
        "output", testing::internal::OutputFlagAlsoCheckEnvVar().c_str()),
    "A format (defaults to \"xml\" but can be specified to be \"json\"), "
    "optionally followed by a colon and an output file name or directory. "
    "A directory is indicated by a trailing pathname separator. "
    "Examples: \"xml:filename.xml\", \"xml::directoryname/\". "
    "If a directory is specified, output files will be created "
    "within that directory, with file-names based on the test "
    "executable's name and, if necessary, made unique by adding "
    "digits.");

GTEST_DEFINE_bool_(
    brief, testing::internal::BoolFromGTestEnv("brief", false),
    "True if only test failures should be displayed in text output.");

GTEST_DEFINE_bool_(print_time,
                   testing::internal::BoolFromGTestEnv("print_time", true),
                   "True if and only if " GTEST_NAME_
                   " should display elapsed time in text output.");

GTEST_DEFINE_bool_(print_utf8,
                   testing::internal::BoolFrom