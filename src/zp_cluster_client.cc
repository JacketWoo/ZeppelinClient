#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "zp_cluster_client.h"
#include "net_utils.h"

ZPClusterClient::ZPClusterClient(const std::vector<IpPort>& meta_hosts, int32_t connect_timeout_ms, int32_t rw_timeout_ms)
	:meta_hosts_(meta_hosts),
	connect_timeout_ms_(connect_timeout_ms),
	rw_timeout_ms_(rw_timeout_ms) {
//TODO
//	Status s = GetClusterInfo();
//	assert(s.ok());
	
}

ZPClusterClient::~ZPClusterClient() {
	
}

Status ZPClusterClient::Connect(const IpPort& server, int32_t* socket_fd) {
  struct addrinfo hint, *servinfo_p, *p;
  std::string sport = std::to_string(server.port);
  int ret, fd;
  bzero(&hint, sizeof(addrinfo));
  hint.ai_family = AF_INET; /* AF_UNSPEC allows for IPv4 and Ipv6 */
  hint.ai_socktype = SOCK_STREAM;
  //hint.ai_flags = AI_PASSIVE /* For Wildcard IP Address */
  ret = getaddrinfo(server.hostname.c_str(), sport.c_str(), reinterpret_cast<const struct addrinfo*>(&hint),
      reinterpret_cast<struct addrinfo**>(&servinfo_p)); 
  if (ret) {
    return Status(Status::kErr, std::string(gai_strerror(ret)));
  }
  for (p = servinfo_p; p != NULL; p = p->ai_next) {
    if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (utils::Connect(fd, p->ai_addr, p->ai_addrlen, connect_timeout_ms_ == -1 ? -1 : connect_timeout_ms_ / 1000) == -1) {
      close(fd);
      continue;
    }
    break;
  }
  freeaddrinfo(servinfo_p);
  if (p == NULL) {
    return Status(Status::kErr, std::string(strerror(errno)));
  }
	current_fd_ = fd;
	socket_fd == NULL ?: (*socket_fd = fd);
  return Status();	
}

Status ZPClusterClient::GetClusterInfo() {
	::ZPMeta::MetaCmdResponse_Pull pull_resp;
	Status s;
	for (std::vector<IpPort>::const_iterator iter = meta_hosts_.begin();
			iter != meta_hosts_.end();
			++iter) {
		if (!(Connect(*iter).ok())) {
			continue;
		}
		if (!(s = Pull(&pull_resp)).ok()) {
			return s;
		}
		RestorePullResponse(pull_resp, &cluster_);
		break;
	}
	return Status();	
}


Status ZPClusterClient::RestorePullResponse(const ::ZPMeta::MetaCmdResponse_Pull& pull_resp, ClusterInfo* cluster_info) {
	::ZPMeta::Node node;
	::Node *local_master = NULL, *local_slave = NULL;
	std::string s_ipport;
	cluster_info->masters.clear();
	cluster_info->nodes.clear();
	cluster_info->total_partition = pull_resp.info_size();
	for (int index = 0; index != cluster_info->total_partition; ++index) {
		const ::ZPMeta::Partitions& p = pull_resp.info(index);

		// master part
		node = p.master();
		s_ipport = node.ip() + ":" + std::to_string(node.port());
		if (cluster_info->nodes.find(s_ipport) == cluster_info->nodes.end()) {
			local_master = new Node();
			local_master->host.hostname = node.ip();
			local_master->host.port = node.port();
			local_master->sock.socket_fd = -1;
			cluster_info->nodes.insert(std::pair<std::string, Node*>(s_ipport, local_master));
		} else {
			local_master = cluster_info->nodes.at(s_ipport);
		}
		cluster_info->masters.insert(std::pair<int32_t, Node*>(index, local_master));
		
		// slave part
		for (int idx = 0; idx != p.slaves_size(); ++idx) {
			node = p.slaves(idx);
			s_ipport = node.ip() + ":" + std::to_string(node.port());
			if (cluster_info->nodes.find(s_ipport) == cluster_info->nodes.end()) {
				local_slave = new Node();
				local_slave->host.hostname = node.ip();
				local_slave->host.port = node.port();
				local_slave->sock.socket_fd = -1;
				cluster_info->nodes.insert(std::pair<std::string, Node*>(s_ipport, local_slave));
			} else {
				local_slave = cluster_info->nodes.at(s_ipport);
			}
			local_master->slaves.push_back(local_slave);
		}
	}
	return Status();
}

Status ZPClusterClient::Pull(::ZPMeta::MetaCmdResponse_Pull* pull_resp) {
	Status s;
	::google::protobuf::Message* cmd = ConstructCommand(kMetaServer, ::ZPMeta::MetaCmd_Type_PULL);
	if (!cmd) {
		return Status(Status::kErr, "construct pull command error");
	}
	if (!(s = SerializeMessage(cmd)).ok()) {
		delete cmd;
	return s;		
	}
	delete cmd;

	std::vector<IpPort>::const_iterator iter;
	for (iter = meta_hosts_.begin(); iter != meta_hosts_.end(); ++iter) {
		if (!(Connect(*iter).ok())) {
			continue;
		}
		if (Send() == -1 || Recv() == -1) {
			close(current_fd_);
		} else {
			break;
		}
	}
	if (iter == meta_hosts_.end()) {
		return Status(Status::kErr, std::string("Send request or Recv response failed, with errno's description: ") + strerror(errno));	
	}
	::ZPMeta::MetaCmdResponse* resp = new ::ZPMeta::MetaCmdResponse();
	resp->ParseFromArray(rbuf_ + MESSAGE_HEADER_LEN, rlen_ - MESSAGE_HEADER_LEN);
	s.Set(resp->status().code());
	if (s.ok()) {
//		memcpy(pull_resp, resp->mutable_pull(), sizeof(*pull_resp));
		pull_resp->CopyFrom(resp->pull());
	}
	delete resp;
	close(current_fd_);
	return s;
}

Status ZPClusterClient::Init(int32_t partition_num) {
	Status s;
	::google::protobuf::Message* cmd = ConstructCommand(kMetaServer, ::ZPMeta::MetaCmd_Type_INIT);
	if (!cmd) {
		return Status(Status::kErr, "construct init command error");
	}
	dynamic_cast<::ZPMeta::MetaCmd*>(cmd)->mutable_init()->set_num(partition_num);
	if (!(s = SerializeMessage(cmd)).ok()) {
		delete cmd;
		return s;
	}
	delete cmd;
	std::vector<IpPort>::const_iterator iter;
	for (iter = meta_hosts_.begin(); iter != meta_hosts_.end(); ++iter) {
		if (!(Connect(*iter).ok())) {
			continue;
		}
		if (Send() == -1 || Recv() == -1) {
			close(current_fd_);
		} else {
			break;
		}
	}
	if (iter == meta_hosts_.end()) {
		return Status(Status::kErr, std::string("Send request or Recv response failed, with errno's description: ") + strerror(errno));
	}
	::ZPMeta::MetaCmdResponse* resp = new ::ZPMeta::MetaCmdResponse();
	resp->ParseFromArray(rbuf_ + MESSAGE_HEADER_LEN, rlen_ - MESSAGE_HEADER_LEN);
	s.Set(resp->status().code(), resp->status().msg());
	delete resp;
	close(current_fd_);
	return s;
}

::google::protobuf::Message* ZPClusterClient::ConstructCommand(ServerType serverType, int32_t commandType) {
	if (serverType == kDataServer) {
//		TODO
//		return ConstructDataCommand(static_cast<::client::Type>(commandType));
	} else if (serverType == kMetaServer) {
		return ConstructMetaCommand(static_cast<::ZPMeta::MetaCmd_Type>(commandType));
	}
	return NULL;
}

::google::protobuf::Message* ZPClusterClient::ConstructMetaCommand(::ZPMeta::MetaCmd_Type commandType) {
	::ZPMeta::MetaCmd* metaCmd = new ::ZPMeta::MetaCmd();
	metaCmd->set_type(commandType);
	switch (commandType) {
		case ZPMeta::MetaCmd_Type_PULL:
			metaCmd->set_allocated_pull(new ::ZPMeta::MetaCmd_Pull());
			break;
		case ZPMeta::MetaCmd_Type_INIT:
			metaCmd->set_allocated_init(new ::ZPMeta::MetaCmd_Init());
			break;
		default:
			//TODO: error handler
			delete metaCmd;
			return NULL;
	}
	return metaCmd;
}

Status ZPClusterClient::SerializeMessage(::google::protobuf::Message* msg_p) {
  int32_t size = msg_p->ByteSize();
  if (size > MAX_SINGLE_MESSAGE_LEN) {
    return Status(Status::kErr, "Message is too long with " + std::to_string(size) + " bytes");
  }
  SerializeMessageHeader(wbuf_, reinterpret_cast<const char*>(msg_p));
  msg_p->SerializeToArray(reinterpret_cast<char*>(wbuf_)+MESSAGE_HEADER_LEN, sizeof(wbuf_)-MESSAGE_HEADER_LEN);
  wlen_ = size + MESSAGE_HEADER_LEN;
  return Status();
}

int ZPClusterClient::Send() {
//  ClearWbuf();
  int nsend = 0, cur_pos = 0;
  while (cur_pos < wlen_) {
//    nsend = send(current_fd_, wbuf_ + cur_pos, wlen_ - cur_pos, 0);
    nsend = write(current_fd_, wbuf_ + cur_pos, wlen_-cur_pos);
    if (nsend <= 0) {
      if (errno == EAGAIN) {
        continue;
      } else {
        break;
      }
    }
    cur_pos += nsend;
  }
  if (cur_pos != wlen_) {
    ClearWbuf();
    return -1;
  }
  ClearWbuf();
  return 0;
}

int ZPClusterClient::Recv() {
  ClearRbuf();
  int nrecv = 0, cur_pos = 0;
  while (cur_pos < MESSAGE_HEADER_LEN) {
//    nrecv = recv(current_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos, 0);
    nrecv = read(current_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos);
    if (nrecv <= 0) {
      if (errno == EAGAIN) {
        continue;
      } else {
        return -1; 
      }
    }
    cur_pos += nrecv;
  }
  rlen_ = static_cast<int32_t>(ntohl(*reinterpret_cast<uint32_t*>(rbuf_))) + MESSAGE_HEADER_LEN;
  while (cur_pos < rlen_) {
    //nrecv = recv(current_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos, 0);
    nrecv = read(current_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos);
    if (nrecv <= 0) {
      if (errno == EAGAIN) {
        continue;
      } else {
        return -1;
      }
    }
    cur_pos += nrecv;
  }
  return 0;
}
