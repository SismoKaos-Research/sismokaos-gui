// A stand-in for external programs in the tests, so that they need no shell:
// the same binary on Linux, macOS and Windows.
//
// clang-format off
//   helper --out TEXT      print TEXT and a newline
//   helper --raw TEXT      print TEXT with \n escapes expanded, no newline after
//   helper --err TEXT      print TEXT and a newline on stderr
//   helper --env VAR       print the value of environment variable VAR
//   helper --sleep MS      wait MS milliseconds
//   helper --exit N        exit with status N
//   helper <anything else> print "<own name> 9.9 fake" (a tool's --version)
// clang-format on
//
// Options are carried out in order; without --exit the status is 0.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : "";
    if (a == "--out") {
      std::printf("%s\n", v);
      ++i;
    } else if (a == "--raw") {
      std::string s = v;
      for (size_t p; (p = s.find("\\n")) != std::string::npos;)
        s.replace(p, 2, "\n");
      std::fputs(s.c_str(), stdout);
      ++i;
    } else if (a == "--err") {
      std::fprintf(stderr, "%s\n", v);
      ++i;
    } else if (a == "--env") {
      const char *e = std::getenv(v);
      std::printf("%s\n", e ? e : "");
      ++i;
    } else if (a == "--sleep") {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(v)));
      ++i;
    } else if (a == "--exit") {
      return std::atoi(v);
    } else {
      std::string name = argv[0];
      const size_t slash = name.find_last_of("/\\");
      if (slash != std::string::npos)
        name = name.substr(slash + 1);
      if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
        name.resize(name.size() - 4);
      std::printf("%s 9.9 fake\n", name.c_str());
      return 0;
    }
  }
  return 0;
}
