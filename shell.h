#include <atomic>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace shell {

struct ExecResult {
  bool is_tle;
  bool is_oom;
  int return_code;
  std::string outs;
  std::string errs;
};

struct ExecArgs {
  bool take_stdin = false;
  bool take_outs = false;
  bool take_errs = false;
  int memory_limit_MB = 1024;
  unsigned timeout_seconds = 3;
};

class ShellProcess {
protected:
  std::vector<std::string> argv;
  ExecArgs args;
  pid_t pid;

  int ins_pipe[2];
  int outs_pipe[2];
  int errs_pipe[2];

  void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      perror("fcntl F_GETFL");
      exit(1);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      perror("fcntl F_SETFL O_NONBLOCK");
      exit(1);
    }
  }

public:
  ShellProcess(
      std::vector<std::string> argv, ExecArgs args = {});

  ExecResult communicate();

  void write_stdin(const std::string &data) {
    while (
        write(ins_pipe[1], data.c_str(), data.size()) == 0)
      ;
  }

  std::string read_stdout() {
    char buffer[BUFSIZ];
    std::string output;
    ssize_t n;
    while ((n = read(outs_pipe[0], buffer,
                sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      output += buffer;
    }
    return output;
  }

  std::string read_stderr() {
    char buffer[BUFSIZ];
    std::string output;
    ssize_t n;
    while ((n = read(errs_pipe[0], buffer,
                sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      output += buffer;
    }
    return output;
  }

  static ExecResult execute(
      std::string cmd, ExecArgs args = {}) {
    ShellProcess p({"sh", "-c", cmd}, args);
    return p.communicate();
  }
};

class GDBController : public ShellProcess {
  std::string unprocessed_response;
  int stdin_fd, stdout_fd, stderr_fd;

  static std::vector<std::string> make_argv(
      std::vector<std::string> argv) {
    std::vector<std::string> ret = {"/usr/bin/gdb",
        "--interpreter=mi3", "-q", "--args"};
    for (auto &str : argv) ret.push_back(str);
    return ret;
  }

public:
  GDBController(
      std::vector<std::string> argv, ExecArgs args = {})
      : ShellProcess(
            make_argv(argv), ExecArgs{.take_stdin = true,
                                 .take_outs = true}) {
    recv_until();
  }

  struct Frame {
    std::string func;
    std::string file;
    std::string line;
  };

  std::vector<Frame> traceback_frames();

  void interrupt() { kill(pid, SIGINT); }

  void send(std::string data) {
    if (data[-1] != '\n') data += '\n';
    write_stdin(data);
  }

  std::string recv_until(
      const std::vector<std::string> &until = {"(gdb)"}) {
    bool stop = false;
    unsigned line_st = 0, line_ed = 0;
    std::string response = unprocessed_response;
    while (!stop) {
      std::string piece = read_stdout();
      response += piece;

      while (!stop && line_ed < response.size()) {
        while (line_ed < response.size() &&
               response[line_ed] != '\n') {
          line_ed++;
        }

        if (line_ed >= response.size()) break;

        std::string currentLine =
            response.substr(line_st, line_ed - line_st);
        for (const auto &u : until) {
          if (currentLine.rfind(u, 0) == 0) {
            stop = true;
            break;
          }
        }

        line_st = line_ed + 1;
        line_ed = line_ed + 1;
      }
    }
    if (line_st < response.size())
      unprocessed_response = response.substr(line_st);
    else
      unprocessed_response.clear();
    return response;
  }

  bool run(std::string cmd = "r", int timeout = 30);
  std::string communicate(std::string data);

  static std::vector<Frame> traceback_compiler(
      std::string cmd, unsigned timeout = 30);

  static std::string get_with_default(
      const std::unordered_map<std::string, std::string>
          &map,
      const std::string &key,
      const std::string &default_value) {
    auto it = map.find(key);
    if (it == map.end()) { return default_value; }
    return it->second;
  }

  static std::vector<std::string> split_string(
      const std::string &str) {
    std::istringstream iss(str);
    std::vector<std::string> result;
    for (std::string token;
         std::getline(iss, token, ' ');) {
      result.push_back(std::move(token));
    }
    return result;
  }
};

} // namespace shell
