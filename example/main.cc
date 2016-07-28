#include <string>
#include <strings.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <arpa/inet.h>

#include "zp_client.h"
#include "string_utils.h"

void usage() {
  fprintf(stderr, "usage:\n"
                  "      zp_cli host port\n");
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    usage();
    return -1;
  }
  std::string host = argv[1];
  int32_t port = atoi(argv[2]);
  ZPClient zp_client(host, port);
  std::string prompt, result, line;
  std::vector<std::string> args_v;
  Status s;
  while (true)
  {
    prompt = host + ":" + std::to_string(port) + " >";
    if (!(s = zp_client.Connect()).ok()) {
      prompt = s.msg() + " >"; 
    }
    s.SetOk();
    while (s.ok()) {
      std::cout << prompt;
      std::getline(std::cin, line);
      args_v.clear();
      utils::GetSeparateArgs(line, &args_v);
  //    assert(!args_v.empty());
      std::transform(args_v[0].begin(), args_v[0].end(), args_v[0].begin(), ::tolower);
      if (args_v[0] == "set") {
        if (args_v.size() != 3) {
          result = "Wrong argument number";
        } else {
          s = zp_client.Set(args_v[1], args_v[2]);
          result = s.msg();
        }
      } else if (args_v[0] == "get") {
        if (args_v.size() != 2) {
          result = "Wrong argument number";
        } else {
          s = zp_client.Get(args_v[1]);
          result = s.msg();
        }
      } else {
        result = "Unkown command";
      }
      std::cout << result << std::endl;
    }
  }
}
