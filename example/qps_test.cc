#include <vector>
#include <string.h>
#include <ctime>
#include <stdio.h>
#include <stdlib.h>

#include "zp_cluster_client.h"


void Usage() {
  fprintf(stderr, "usage:\n"
                  "      qps_test host port command req_count\n");
}

std::string GenRandStr(int32_t len) {
	std::string str;
	int32_t r;
	char cap;
	while (len-- > 0) {	
		r = rand();
		cap = (r >> 5) & 0x1 ? 'a' : 'A';
		str.push_back(static_cast<char>(cap+rand()%26));	
	}
	return str;
}

enum CommandType{
	kSet,
	kGet,
	kAll
};

int main(int argc, char *argv[]) {
	if (argc != 5) {
			Usage();
			return -1;
	}
	std::vector<IpPort> hosts;
  std::string host = argv[1];
  int32_t port = atoi(argv[2]);
	int32_t req_count = atoi(argv[4]);
	hosts.push_back({host, port});
  ZPClusterClient zp_client(hosts);
	srand(time(NULL));
	std::string prefix = GenRandStr(10);
		
	std::string key, value;
	CommandType type;
	int r;
	Status s;

	if (!strcasecmp(argv[3], "set")) {
		type = kSet;	
	} else if (!strcasecmp(argv[3], "get")) {
		type = kGet;	
	} else if (!strcasecmp(argv[3], "all")) {
		type = kAll;	
	}
	
	time_t start = time(NULL);
	int32_t total = req_count;
	while (req_count-- > 0) {
		printf("%d\n", req_count);
//		r = rand();
		r = req_count;
		key = prefix.append(reinterpret_cast<char*>(&r), sizeof(r));
		if (type == kGet || type == kAll) {
			s = zp_client.Get(key);		
		} else if (type == kSet || type == kAll) {
			value.assign(reinterpret_cast<char*>(&r), sizeof(r));
			s = zp_client.Set(key, value);
		}
		if (!s.ok()) {
			fprintf(stderr, "command's execution error\n");
			exit(-1);
		}
	}
	time_t end = time(NULL);
	fprintf(stderr, "total request count: %d, total used time: %lds, even qps: %ld\n", total, end-start, total/(end-start+1));
}
