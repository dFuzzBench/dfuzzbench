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
// Author: wan@google.com (Zhanyong Wan), vladl@google.com (Vlad Losev)
//
// This file implements death tests.

#include "gtest/gtest-death-test.h"
#include "gtest/internal/gtest-port.h"
#include "gtest/internal/custom/gtest.h"

#if GTEST_HAS_DEATH_TEST

# if GTEST_OS_MAC
#  include <crt_externs.h>
# endif  // GTEST_OS_MAC

# include <errno.h>
# include <fcntl.h>
# include <limits.h>

# if GTEST_OS_LINUX
#  include <signal.h>
# endif  // GTEST_OS_LINUX

# include <stdarg.h>

# if GTEST_OS_WINDOWS
#  include <windows.h>
# else
#  include <sys/mman.h>
#  include <sys/wait.h>
# endif  // GTEST_OS_WINDOWS

# if GTEST_OS_QNX
#  include <spawn.h>
# endif  // GTEST_OS_QNX

#endif  // GTEST_HAS_DEATH_TEST

#include "gtest/gtest-message.h"
#include "gtest/internal/gtest-string.h"

// Indicates that this translation unit is part of Google Test's
// implementation.  It must come before gtest-internal-inl.h is
// included, or there will be a compiler error.  This trick exists to
// prevent the accidental inclusion of gtest-internal-inl.h in the
// user's code.
#define GTEST_IMPLEMENTATION_ 1
#include "src/gtest-internal-inl.h"
#undef GTEST_IMPLEMENTATION_

namespace testing {

// Constants.

// The default death test style.
static const char kDefaultDeathTestStyle[] = "fast";

GTEST_DEFINE_string_(
    death_test_style,
    internal::StringFromGTestEnv("death_test_style", kDefaultDeathTestStyle),
    "Indicates how to run a death test in a forked child process: "
    "\"threadsafe\" (child process re-executes the test binary "
    "from the beginning, running only the specific death test) or "
    "\"fast\" (child process runs the death test immediately "
    "after forking).");

GTEST_DEFINE_bool_(
    death_test_use_fork,
    internal::BoolFromGTestEnv("death_test_use_fork", false),
    "Instructs to use fork()/_exit() instead of clone() in death tests. "
    "Ignored and always uses fork() on POSIX systems where clone() is not "
    "implemented. Useful when running under valgrind or similar tools if "
    "those do not support clone(). Valgrind 3.3.1 will just fail if "
    "it sees an unsupported combination of clone() flags. "
    "It is not recommended to use this flag w/o valgrind though it will "
    "work in 99% of the cases. Once valgrind is fixed, this flag will "
    "most likely be removed.");

namespace internal {
GTEST_DEFINE_string_(
    internal_run_death_test, "",
    "Indicates the file, line number, temporal index of "
    "the single death test to run, and a file descriptor to "
    "which a success code may be sent, all separated by "
    "the '|' characters.  This flag is specified if and only if the current "
    "process is a sub-process launched for running a thread-safe "
    "death test.  FOR INTERNAL USE ONLY.");
}  // namespace internal

#if GTEST_HAS_DEATH_TEST

namespace internal {

// Valid only for fast death tests. Indicates the code is running in the
// child process of a fast style death test.
# if !GTEST_OS_WINDOWS
static bool g_in_fast_death_test_child = false;
# endif

// Returns a Boolean value indicating whether the caller is currently
// executing in the context of the death test child process.  Tools such as
// Valgrind heap checkers may need this to modify their behavior in death
// tests.  IMPORTANT: This is an internal utility.  Using it may break the
// implementation of death tests.  User code MUST NOT use it.
bool InDeathTestChild() {
# if GTEST_OS_WINDOWS

  // On Windows, death tests are thread-safe regardless of the value of the
  // death_test_style flag.
  return !GTEST_FLAG(internal_run_death_test).empty();

# else

  if (GTEST_FLAG(death_test_style) == "threadsafe")
    return !GTEST_FLAG(internal_run_death_test).empty();
  else
    return g_in_fast_death_test_child;
#endif
}

}  // namespace internal

// ExitedWithCode constructor.
ExitedWithCode::ExitedWithCode(int exit_code) : exit_code_(exit_code) {
}

// ExitedWithCode function-call operator.
bool ExitedWithCode::operator()(int exit_status) const {
# if GTEST_OS_WINDOWS

  return exit_status == exit_code_;

# else

  return WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == exit_code_;

# endif  // GTEST_OS_WINDOWS
}

# if !GTEST_OS_WINDOWS
// KilledBySignal constructor.
KilledBySignal::KilledBySignal(int signum) : signum_(signum) {
}

// KilledBySignal function-call operator.
bool KilledBySignal::operator()(int exit_status) const {
#  if defined(GTEST_KILLED_BY_SIGNAL_OVERRIDE_)
  {
    bool result;
    if (GTEST_KILLED_BY_SIGNAL_OVERRIDE_(signum_, exit_status, &result)) {
      return result;
    }
  }
#  endif  // defined(GTEST_KILLED_BY_SIGNAL_OVERRIDE_)
  return WIFSIGNALED(exit_status) && WTERMSIG(exit_status) == signum_;
}
# endif  // !GTEST_OS_WINDOWS

namespace internal {

// Utilities needed for death tests.

// Generates a textual description of a given exit code, in the format
// specified by wait(2).
static std::string ExitSummary(int exit_code) {
  Message m;

# if GTEST_OS_WINDOWS

  m << "Exited with exit status " << exit_code;

# else

  if (WIFEXITED(exit_code)) {
    m << "Exited with exit status " << WEXITSTATUS(exit_code);
  } else if (WIFSIGNALED(exit_code)) {
    m << "Terminated by signal " << WTERMSIG(exit_code);
  }
#  ifdef WCOREDUMP
  if (WCOREDUMP(exit_code)) {
    m << " (core dumped)";
  }
#  endif
# endif  // GTEST_OS_WINDOWS

  return m.GetString();
}

// Returns true if exit_status describes a process that was terminated
// by a signal, or exited normally with a nonzero exit code.
bool ExitedUnsuccessfully(int exit_status) {
  return !ExitedWithCode(0)(exit_status);
}

# if !GTEST_OS_WINDOWS
// Generates a textual failure message when a death test finds more than
// one thread running, or cannot determine the number of threads, prior
// to executing the given statement.  It is the responsibility of the
// caller not to pass a thread_count of 1.
static std::string DeathTestThreadWarning(size_t thread_count) {
  Message msg;
  msg << "Death tests use fork(), which is unsafe particularly"
      << " in a threaded context. For this test, " << GTEST_NAME_ << " ";
  if (thread_count == 0)
    msg << "couldn't detect the number of threads.";
  else
    msg << "detected " << thread_count << " threads.";
  return msg.GetString();
}
# endif  // !GTEST_OS_WINDOWS

// Flag characters for reporting a death test that did not die.
static const char kDeathTestLived = 'L';
static const char kDeathTestReturned = 'R';
static const char kDeathTestThrew = 'T';
static const char kDeathTestInternalError = 'I';

// An enumeration describing all of the possible ways that a death test can
// conclude.  DIED means that the process died while executing the test
// code; LIVED means that process lived beyond the end of the test code;
// RETURNED means that the test statement attempted to execute a return
// statement, which is not allowed; THREW means that the test statement
// returned control by throwing an exception.  IN_PROGRESS means the test
// has not yet concluded.
// TODO(vladl@google.com): Unify names and possibly values for
// AbortReason, DeathTestOutcome, and flag characters above.
enum DeathTestOutcome { IN_PROGRESS, DIED, LIVED, RETURNED, THREW };

// Routine for aborting the program which is safe to call from an
// exec-style death test child process, in which case the error
// message is propagated back to the parent process.  Otherwise, the
// message is simply printed to stderr.  In either case, the program
// then exits with status 1.
void DeathTestAbort(const std::string& message) {
  // On a POSIX system, this function may be called from a threadsafe-style
  // death test child process, which operates on a very small stack.  Use
  // the heap for any additional non-minuscule memory requirements.
  const InternalRunDeathTestFlag* const flag =
      GetUnitTestImpl()->internal_run_death_test_flag();
  if (flag != NULL) {
    FILE* parent = posix::FDOpen(flag->write_fd(), "w");
    fputc(kDeathTestInternalError, parent);
    fprintf(parent, "%s", message.c_str());
    fflush(parent);
    _exit(1);
  } else {
    fprintf(stderr, "%s", message.c_str());
    fflush(stderr);
    posix::Abort();
  }
}

// A replacement for CHECK that calls DeathTestAbort if the assertion
// fails.
# define GTEST_DEATH_TEST_CHECK_(expression) \
  do { \
    if (!::testing::internal::IsTrue(expression)) { \
      DeathTestAbort( \
          ::std::string("CHECK failed: File ") + __FILE__ +  ", line " \
          + ::testing::internal::StreamableToString(__LINE__) + ": " \
          + #expression); \
    } \
  } while (::testing::internal::AlwaysFalse())

// This macro is similar to GTEST_DEATH_TEST_CHECK_, but it is meant for
// evaluating any system call that fulfills two conditions: it must return
// -1 on failure, and set errno to EINTR when it is interrupted and
// should be tried again.  The macro expands to a loop that repeatedly
// evaluates the expression as long as it evaluates to -1 and sets
// errno to EINTR.  If the expression evaluates to -1 but errno is
// something other than EINTR, DeathTestAbort is called.
# define GTEST_DEATH_TEST_CHECK_SYSCALL_(expression) \
  do { \
    int gtest_retval; \
    do { \
      gtest_retval = (expression); \
    } while (gtest_retval == -1 && errno == EINTR); \
    if (gtest_retval == -1) { \
      DeathTestAbort( \
          ::std::string("CHECK failed: File ") + __FILE__ + ", line " \
          + ::testing::internal::StreamableToString(__LINE__) + ": " \
          + #expression + " != -1"); \
    } \
  } while (::testing::internal::AlwaysFalse())

// Returns the message describing the last system error in errno.
std::string GetLastErrnoDescription() {
    return errno == 0 ? "" : posix::StrError(errno);
}

// This is called from a death test parent process to read a failure
// message from the death test child process and log it with the FATAL
// severity. On Windows, the message is read from a pipe handle. On other
// platforms, it is read from a file descriptor.
static void FailFromInternalError(int fd) {
  Message error;
  char buffer[256];
  int num_read;

  do {
    while ((num_read = posix::Read(fd, buffer, 255)) > 0) {
      buffer[num_read] = '\0';
      error << buffer;
    }
  } while (num_read == -1 && errno == EINTR);

  if (num_read == 0) {
    GTEST_LOG_(FATAL) << error.GetString();
  } else {
    const int last_error = errno;
    GTEST_LOG_(FATAL) << "Error while reading death test internal: "
                      << GetLastErrnoDescription() << " [" << last_error << "]";
  }
}

// Death test constructor.  Increments the running death test count
// for the current test.
DeathTest::DeathTest() {
  TestInfo* const info = GetUnitTestImpl()->current_test_info();
  if (info == NULL) {
    DeathTestAbort("Cannot run a death test outside of a TEST or "
                   "TEST_F construct");
  }
}

// Creates and returns a death test by dispatching to the current
// death test factory.
bool DeathTest::Create(const char* statement, const RE* regex,
                       const char* file, int line, DeathTest** test) {
  return GetUnitTestImpl()->death_test_factory()->Create(
      statement, regex, file, line, test);
}

const char* DeathTest::LastMessage() {
  return last_death_test_message_.c_str();
}

void DeathTest::set_last_death_test_message(const std::string& message) {
  last_death_test_message_ = message;
}

std::string DeathTest::last_death_test_message_;

// Provides cross platform implementation for some death functionality.
class DeathTestImpl : public DeathTest {
 protected:
  DeathTestImpl(const char* a_statement, const RE* a_regex)
      : statement_(a_statement),
        regex_(a_regex),
        spawned_(false),
        status_(-1),
        outcome_(IN_PROGRESS),
        read_fd_(-1),
        write_fd_(-1) {}

  // read_fd_ is expected to be closed and cleared by a derived class.
  ~DeathTestImpl() { GTEST_DEATH_TEST_CHECK_(read_fd_ == -1); }

  void Abort(AbortReason reason);
  virtual bool Passed(bool status_ok);

  const char* statement() const { return statement_; }
  const RE* regex() const { return regex_; }
  bool spawned() const { return spawned_; }
  void set_spawned(bool is_spawned) { spawned_ = is_spawned; }
  int status() const { return status_; }
  void set_status(int a_status) { status_ = a_status; }
  DeathTestOutcome outcome() const { return outcome_; }
  void set_outcome(DeathTestOutcome an_outcome) { outcome_ = an_outcome; }
  int read_fd() const { return read_fd_; }
  void set_read_fd(int fd) { read_fd_ = fd; }
  int write_fd() const { return write_fd_; }
  void set_write_fd(int fd) { write_fd_ = fd; }

  // Called in the parent process only. Reads the result code of the death
  // test child process via a pipe, interprets it to set the outcome_
  // member, and closes read_fd_.  Outputs diagnostics and terminates in
  // case of unexpected codes.
  void ReadAndInterpretStatusByte();

 private:
  // The textual content of the code this object is testing.  This class
  // doesn't own this string and should not attempt to delete it.
  const char* const statement_;
  // The regular expression which test output must match.  DeathTestImpl
  // doesn't own this object and should not attempt to delete it.
  const RE* const regex_;
  // True if the death test child process has been successfully spawned.
  bool spawned_;
  // The exit status of the child process.
  int status_;
  // How the death test concluded.
  DeathTestOutcome outcome_;
  // Descriptor to the read end of the pipe to the child process.  It is
  // always -1 in the child process.  The child keeps its write end of the
  // pipe in write_fd_.
  int read_fd_;
  // Descriptor to the child's write end of the pipe to the parent process.
  // It is always -1 in the parent process.  The parent keeps its end of the
  // pipe in read_fd_.
  int write_fd_;
};

// Called in the parent process only. Reads the result code of the death
// test child process via a pipe, interprets it to set the outcome_
// member, and closes read_fd_.  Outputs diagnostics and terminates in
// case of unexpected codes.
void DeathTestImpl::ReadAndInterpretStatusByte() {
  char flag;
  int bytes_read;

  // The read() here blocks until data is available (signifying the
  // failure of the death test) or until the pipe is closed (signifying
  // its success), so it's okay to call this in the parent before
  // the child process has exited.
  do {
    bytes_read = posix::Read(read_fd(), &flag, 1);
  } while (bytes_read == -1 && errno == EINTR);

  if (bytes_read == 0) {
    set_outcome(DIED);
  } else if (bytes_read == 1) {
    switch (flag) {
      case kDeathTestReturned:
        set_outcome(RETURNED);
        break;
      case kDeathTestThrew:
        set_outcome(THREW);
        break;
      case kDeathTestLived:
        set_outcome(LIVED);
        break;
      case kDeathTestInternalError:
        FailFromInternalError(read_fd());  // Does not return.
        break;
      default:
        GTEST_LOG_(FATAL) << "Death test child process reported "
                          << "unexpected status byte ("
                          << static_cast<unsigned int>(flag) << ")";
    }
  } else {
    GTEST_LOG_(FATAL) << "Read from death test child process failed: "
                      << GetLastErrnoDescription();
  }
  GTEST_DEATH_TEST_CHECK_SYSCALL_(posix::Close(read_fd()));
  set_read_fd(-1);
}

// Signals that the death test code which should have exited, didn't.
// Should be called only in a death test child process.
// Writes a status byte to the child's status file descriptor, then
// calls _exit(1).
void DeathTestImpl::Abort(AbortReason reason) {
  // The parent process considers the death test to be a failure if
  // it finds any data in our pipe.  So, here we write a single flag byte
  // to the pipe, then exit.
  const char status_ch =
      reason == TEST_DID_NOT_DIE ? kDeathTestLived :
      reason == TEST_THREW_EXCEPTION ? kDeathTestThrew : kDeathTestReturned;

  GTEST_DEATH_TEST_CHECK_SYSCALL_(posix::Write(write_fd(), &status_ch, 1));
  // We are leaking the descriptor here because on some platforms (i.e.,
  // when built as Windows DLL), destructors of global objects will still
  // run after calling _exit(). On such systems, write_fd_ will be
  // indirectly closed from the destructor of UnitTestImpl, causing double
  // close if it is also closed here. On debug configurations, double close
  // may assert. As there are no in-process buffers to flush here, we are
  // relying on the OS to close the descriptor after the process terminates
  // when the destructors are not run.
  _exit(1);  // Exits w/o any normal exit hooks (we were supposed to crash)
}

// Returns an indented copy of stderr output for a death test.
// This makes distinguishing death test output lines from regular log lines
// much easier.
static ::std::string FormatDeathTestOutput(const ::std::string& output) {
  ::std::string ret;
  for (size_t at = 0; ; ) {
    const size_t line_end = output.find('\n', at);
    ret += "[  DEATH   ] ";
    if (line_end == ::std::string::npos) {
      ret += output.substr(at);
      break;
    }
    ret += output.substr(at, line_end + 1 - at);
    at = line_end + 1;
  }
  return ret;
}

// Assesses the success or failure of a death test, using both private
// members which have previously been set, and one argument:
//
// Private data members:
//   outcome:  An enumeration describing how the death test
//             concluded: DIED, LIVED, THREW, or RETURNED.  The death test
//             fails in the latter three cases.
//   status:   The exit status of the child process. On *nix, it is in the
//             in the format specified by wait(2). On Windows, this is the
//             value supplied to the ExitProcess() API or a numeric code
//             of the exception that terminated the program.
//   regex:    A regular expression object to be applied to
//             the test's captured standard error output; the death test
//             fails if it does not match.
//
// Argument:
//   status_ok: true if exit_status is acceptable in the context of
//              this particular death test, which fails if it is false
//
// Returns true iff all of the above conditions are met.  Otherwise, the
// first failing condition, in the order given above, is the one that is
// reported. Also sets the last death test message string.
bool DeathTestImpl::Passed(bool status_ok) {
  if (!spawned())
    return false;

  const std::string error_message = GetCapturedStderr();

  bool success = false;
  Message buffer;

  buffer << "Death test: " << statement() << "\n";
  switch (outcome()) {
    case LIVED:
      buffer << "    Result: failed to die.\n"
             << " Error msg:\n" << FormatDeathTestOutput(error_message);
      break;
    case THREW:
      buffer << "    Result: threw an exception.\n"
             << " Error msg:\n" << FormatDeathTestOutput(error_message);
      break;
    case RETURNED:
      buffer << "    Result: illegal return in test statement.\n"
             << " Error msg:\n" << FormatDeathTestOutput(error_message);
      break;
    case DIED:
      if (status_ok) {
        const bool matched = RE::PartialMatch(error_message.c_str(), *regex());
        if (matched) {
          success = true;
        } else {
          buffer << "    Result: died but not with expected error.\n"
                 << "  Expected: " << regex()->pattern() << "\n"
                 << "Actual msg:\n" << FormatDeathTestOutput(error_message);
        }
      } else {
        buffer << "    Result: died but not with expected exit code:\n"
               << "            " << ExitSummary(status()) << "\n"
               << "Actual msg:\n" << FormatDeathTestOutput(error_message);
      }
      break;
    case IN_PROGRESS:
    default:
      GTEST_LOG_(FATAL)
          << "DeathTest::Passed somehow called before conclusion of test";
  }

  DeathTest::set_last_death_test_message(buffer.GetString());
  return success;
}

# if GTEST_OS_WINDOWS
// WindowsDeathTest implements death tests on Windows. Due to the
// specifics of starting new processes on Windows, death tests there are
// always threadsafe, and Google Test considers the
