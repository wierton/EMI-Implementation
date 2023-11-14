#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <regex>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "shell.h"

using namespace shell;

ShellProcess::ShellProcess(
    std::vector<std::string> _argv, ExecArgs _args)
    : argv(std::move(_argv)), args(std::move(_args)) {
  if (pipe(ins_pipe) != 0 || pipe(outs_pipe) != 0 ||
      pipe(errs_pipe) != 0) {
    perror("pipe");
    exit(1);
  }

  pid = fork();

  if (pid == 0) {
    setpgrp();

    /* set time limit */
    struct rlimit rl_tle;
    getrlimit(RLIMIT_CPU, &rl_tle);
    rl_tle.rlim_cur = args.timeout_seconds; // seconds
    if (setrlimit(RLIMIT_CPU, &rl_tle) < 0) {
      perror("setrlimit");
      exit(1);
    }

    /* set memory limit */
    struct rlimit rl_oom;
    getrlimit(RLIMIT_AS, &rl_oom);
    rl_oom.rlim_cur = args.memory_limit_MB * 1024 * 1024;
    if (setrlimit(RLIMIT_AS, &rl_oom) < 0) {
      perror("setrlimit");
      exit(1);
    }

    /* create pipe */
    if (!args.take_stdin)
      close(0);
    else
      dup2(ins_pipe[0], 0);
    if (args.take_outs) dup2(outs_pipe[1], 1);
    if (args.take_errs) dup2(errs_pipe[1], 2);
    close(ins_pipe[0]);
    close(ins_pipe[1]);
    close(outs_pipe[0]);
    close(outs_pipe[1]);
    close(errs_pipe[0]);
    close(errs_pipe[1]);

    /* prepare arguments */
    std::vector<char *> c_argv;
    for (auto &str : argv)
      c_argv.push_back((char *)str.c_str());
    c_argv.push_back(NULL);

    execvp(c_argv[0], c_argv.data());
    perror("execvp");
    exit(1);
  } else {
    set_nonblocking(ins_pipe[1]);
    set_nonblocking(outs_pipe[0]);
    set_nonblocking(errs_pipe[0]);

    close(ins_pipe[0]);
    close(outs_pipe[1]);
    close(errs_pipe[1]);
  }
}

ExecResult ShellProcess::communicate() {
  std::string outs_str, errs_str;
  int status;
  bool is_oom = false;
  bool is_tle = false;
  unsigned delay(1);
  unsigned remaining(args.timeout_seconds * 1000);
  unsigned max_delay(1000);
  unsigned min_delay(1);

  auto start = std::chrono::steady_clock::now();
  while (true) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(outs_pipe[0], &fds);
    FD_SET(errs_pipe[0], &fds);

    delay =
        std::min(std::min(delay * 2, remaining), max_delay);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = delay * 1000;

    // std::cout << "wait for " << delay << " msecs\n";
    if (select(FD_SETSIZE, &fds, nullptr, nullptr,
            &timeout) == -1) {
      perror("select");
      exit(1);
    }

    if (FD_ISSET(outs_pipe[0], &fds)) {
      outs_str += read_stdout();
      delay = min_delay;
    }

    if (FD_ISSET(errs_pipe[0], &fds)) {
      errs_str += read_stderr();
      delay = min_delay;
    }

    /* check memory usage */
    struct rusage usage;
    getrusage(RUSAGE_CHILDREN, &usage);
    if (usage.ru_maxrss > args.memory_limit_MB * 1024) {
      is_oom = true;
      break;
    }

    /* check time usage */
    auto now = std::chrono::steady_clock::now();
    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            now - start);
    if (elapsed_seconds.count() >= args.timeout_seconds) {
      is_tle = true;
      break;
    }

    int wait_ret = waitpid(pid, &status, WNOHANG);
    if (is_tle || is_oom || wait_ret != 0) break;
  }

  return {is_tle || WEXITSTATUS(status) == 152,
      is_oom || WEXITSTATUS(status) == 139,
      WIFEXITED(status) ? WEXITSTATUS(status) : -1,
      outs_str, errs_str};
}

bool GDBController::run(
    std::string cmd, int timeout_seconds) {
  send(cmd);
  recv_until();
  std::string outs_str;
  auto start = std::chrono::steady_clock::now();

  unsigned delay(1);
  unsigned remaining(timeout_seconds * 1000);
  unsigned max_delay(1000);
  unsigned min_delay(1);
  while (true) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(outs_pipe[0], &fds);

    delay =
        std::min(std::min(delay * 2, remaining), max_delay);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = delay * 1000;

    if (select(FD_SETSIZE, &fds, nullptr, nullptr,
            &timeout) == -1) {
      perror("select");
      exit(1);
    }

    if (FD_ISSET(outs_pipe[0], &fds)) {
      outs_str += read_stdout();
      delay = min_delay;
    }

    if (outs_str.find("(gdb)") != std::string::npos) {
      break;
    }
    if (outs_str.find('\n') != std::string::npos) {
      size_t pos = outs_str.rfind('\n');
      outs_str = outs_str.substr(pos + 1);
    }

    /* check memory usage */
    struct rusage usage;
    getrusage(RUSAGE_CHILDREN, &usage);
    if (usage.ru_maxrss > args.memory_limit_MB * 1024) {
      break;
    }

    /* check time usage */
    auto now = std::chrono::steady_clock::now();
    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            now - start);
    if (elapsed_seconds.count() >= timeout_seconds) break;
  }
  if (outs_str.find("(gdb)") == std::string::npos) {
    interrupt();
    return false;
  }
  return true;
}

std::string GDBController::communicate(std::string data) {
  send(data);
  std::string response = recv_until({"^done", "*stopped"});
  response += recv_until({"(gdb)"});
  return response;
}

std::vector<GDBController::Frame>
GDBController::traceback_frames() {
  std::string response = communicate("-stack-list-frames");

  std::regex re(R"(^\^done,stack=\[frame=(.*)\]$)");
  std::smatch match;
  // Replace with captured group
  std::string replaced_response =
      std::regex_replace(response, re, "$1");

  // Split response by ",frame="
  std::regex split_re(",frame=");
  std::sregex_token_iterator iter(replaced_response.begin(),
      replaced_response.end(), split_re, -1);
  std::sregex_token_iterator end;

  std::vector<Frame> frames;
  for (; iter != end; ++iter) {
    std::unordered_map<std::string, std::string> frame_dict;
    std::string data = *iter;

    // Find key-value pairs
    std::regex kv_re(R"a(\b([-\w]+)="([^"]+)")a");
    auto words_begin = std::sregex_iterator(
        data.begin(), data.end(), kv_re);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin;
         i != words_end; ++i) {
      std::smatch match = *i;
      frame_dict[match[1].str()] = match[2].str();
    }

    Frame frame;
    frame.func = get_with_default(frame_dict, "func", "??");
    frame.file =
        get_with_default(frame_dict, "fullname", "??");
    frame.line = get_with_default(frame_dict, "line", "??");
    frames.push_back(frame);
  }
  return frames;
}

std::vector<GDBController::Frame>
GDBController::traceback_compiler(
    std::string cmd, unsigned timeout) {
  auto argv = split_string(cmd);
  GDBController gdb(argv);
  gdb.communicate("set print inferior-events off");
  // gdb.communicate("set auto-solib-add off");
  gdb.communicate("set breakpoint pending on");
  gdb.communicate("set follow-fork-mode child");
  gdb.communicate("set disable-randomization on");
  gdb.communicate("b exit");
  gdb.communicate("b abort");
  gdb.communicate("b llvm::sys::PrintStackTrace");
  // True means normally exits, False means timeout
  gdb.run("r", timeout);
  return gdb.traceback_frames();
}
