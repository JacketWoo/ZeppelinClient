#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#include "zp_client.h"
#include "net_utils.h"

ZPClient::ZPClient(const std::string& hostname, const int32_t port) :
  hostname_(hostname),
  port_(port),
  wlen_(0),
  rlen_(0) {
    bzero(wbuf_, sizeof(wbuf_));
    bzero(rbuf_, sizeof(rbuf_));
}

ZPClient::~ZPClient() {}

Status ZPClient::Connect(int timeout) {
  struct addrinfo hint, *servinfo_p, *p;
  std::string sport = std::to_string(port_);
  int ret, fd;
  bzero(&hint, sizeof(addrinfo));
  hint.ai_family = AF_INET; /* AF_UNSPEC allows for IPv4 and Ipv6 */
  hint.ai_socktype = SOCK_STREAM;
  //hint.ai_flags = AI_PASSIVE /* For Wildcard IP Address */
  ret = getaddrinfo(hostname().c_str(), sport.c_str(), reinterpret_cast<const struct addrinfo*>(&hint),
      reinterpret_cast<struct addrinfo**>(&servinfo_p)); 
  if (ret) {
    return Status(Status::kErr, std::string(gai_strerror(ret)));
  }
  for (p = servinfo_p; p != NULL; p = p->ai_next) {
    if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (utils::Connect(fd, p->ai_addr, p->ai_addrlen, timeout) == -1) {
      close(fd);
      continue;
    }
    break;
  }
  freeaddrinfo(servinfo_p);
  if (p == NULL) {
    return Status(Status::kErr, std::string(strerror(errno)));
  }
  socket_fd_ = fd;
  return Status();
}

Status ZPClient::Set(const std::string& key, const std::string& value, const std::string& uuid) {
  Status s;
  ::client::CmdRequest* c_p = new ::client::CmdRequest();
  ConstructSetRequest(c_p, key, value, uuid);
  if (!(s = SerializeMessage(c_p)).ok()) {
    delete c_p;
    return s;
  }
  delete c_p;
  if (Send() == -1 || Recv() == -1) {
    return Status(Status::kErr, std::string("Send request or Recv response failed, with errno's description: ") + strerror(errno));
  }
  ::client::CmdResponse* r_p = new ::client::CmdResponse();
  r_p->ParseFromArray(rbuf_ + MESSAGE_HEADER_LEN, rlen_ - MESSAGE_HEADER_LEN);
  if (r_p->set().code() == 0) {
    s.SetOk(r_p->set().msg());
  } else {
    s.SetErr(r_p->set().msg());
  }
  delete r_p; 
  return s;
}

Status ZPClient::Get(const std::string& key, const std::string& uuid) {
  Status s;
  client::CmdRequest* c_p = new client::CmdRequest();
  ConstructGetRequest(c_p, key, uuid);
  if (!(s = SerializeMessage(c_p)).ok()) {
    delete c_p;
    return s;
  }
  delete c_p;
  if (Send() == -1 || Recv() == -1) {
    return Status(Status::kErr, std::string("Send request or Recv response failed, with errno's description: ") + strerror(errno));
  }
  ::client::CmdResponse* r_p = new ::client::CmdResponse();
  r_p->ParseFromArray(rbuf_ + MESSAGE_HEADER_LEN, rlen_ - MESSAGE_HEADER_LEN);
  if (r_p->mutable_get()->code() == client::StatusCode::kOk) {
    s.SetOk(r_p->get().value());
  } else if (r_p->mutable_get()->code() == client::StatusCode::kNotFound) {
    s.SetErr("(nil)");
  } else {
    s.SetErr(r_p->get().value());
  }
  delete r_p;
  return s;
}

void ZPClient::ConstructSetRequest(::client::CmdRequest* msg_p, const std::string& key, const std::string& value, const std::string& uuid) {
  ::client::CmdRequest::Set* p_sub = msg_p->mutable_set();
  msg_p->set_type(::client::SET);
  p_sub->set_key(key);
  p_sub->set_value(value);
  p_sub->set_uuid(uuid);
}

void ZPClient::ConstructGetRequest(::client::CmdRequest* msg_p, const std::string& key, const std::string& uuid) {
  ::client::CmdRequest::Get* p_sub = msg_p->mutable_get();
  msg_p->set_type(::client::GET);
  p_sub->set_key(key);
  p_sub->set_uuid(uuid);
}

Status ZPClient::SerializeMessage(::client::CmdRequest* msg_p) {
  int32_t size = msg_p->ByteSize();
  if (size > MAX_SINGLE_MESSAGE_LEN) {
    return Status(Status::kErr, "Message is too long with " + std::to_string(size) + " bytes");
  }
  SerializeMessageHeader(wbuf_, reinterpret_cast<const char*>(msg_p));
  msg_p->SerializeToArray(reinterpret_cast<char*>(wbuf_)+MESSAGE_HEADER_LEN, sizeof(wbuf_)-MESSAGE_HEADER_LEN);
  wlen_ = size + MESSAGE_HEADER_LEN;
  return Status();
}

int ZPClient::Send() {
//  ClearWbuf();
  int nsend = 0, cur_pos = 0;
  while (cur_pos < wlen_) {
//    nsend = send(socket_fd_, wbuf_ + cur_pos, wlen_ - cur_pos, 0);
    nsend = write(socket_fd_, wbuf_ + cur_pos, wlen_-cur_pos);
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

int ZPClient::Recv() {
  ClearRbuf();
  int nrecv = 0, cur_pos = 0;
  while (cur_pos < MESSAGE_HEADER_LEN) {
//    nrecv = recv(socket_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos, 0);
    nrecv = read(socket_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos);
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
    //nrecv = recv(socket_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos, 0);
    nrecv = read(socket_fd_, rbuf_+cur_pos, sizeof(rbuf_)-cur_pos);
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
