#ifndef ZP_CLIENT_H
#define ZP_CLIENT_H

#include <stdint.h>

#include "proto_utils.h"
#include "status.h"
#include "client.pb.h"

using ::utils::Message;

class ZPClient {
#define MAX_SINGLE_MESSAGE_LEN 102400
#define MESSAGE_HEADER_LEN 4
  public:
    ZPClient(const std::string& hostname = "127.0.0.1", const int32_t port = 9221);
    virtual ~ZPClient();
    
    Status Connect(int timeout = -1);

    Status Set(const std::string& key, const std::string& value, const std::string& uuid = "");
    Status Get(const std::string& key, const std::string& uuid = "");
    std::string hostname() {
      return hostname_;
    }
    int32_t port() {
      return port_;
    }
    int32_t socket_fd() {
      return socket_fd_;
    }
    
  private:

    inline void ConstructSetRequest(::client::CmdRequest* msg_p, const std::string& key, const std::string& value, const std::string& uuid);
    inline void ConstructGetRequest(::client::CmdRequest* msg_p, const std::string& key, const std::string& uuid);
    inline Status SerializeMessage(::client::CmdRequest* msg_p);
    int Send();
    int Recv();
    int SendAndRecv();

    void ClearWbuf() {
      bzero(wbuf_, sizeof(wbuf_));
      wlen_ = 0;
    }

    void ClearRbuf() {
      bzero(rbuf_, sizeof(rbuf_));
      rlen_ = 0;
    }

    /*
     * Using these methods is to privide a convinient subsitution of the way to construct and parse the message header
     */
    inline void SerializeMessageHeader(char* dst_p, const char* src_p, int32_t header_len = 4) {
      (void)header_len;
      *reinterpret_cast<uint32_t*>(dst_p) = htonl(reinterpret_cast<const Message*>(src_p)->ByteSize());
    }

    inline void ParseMessageHeader(const char* src_p, char* dst_p, int32_t header_len = 4) {
      (void)header_len;
      *reinterpret_cast<uint32_t*>(dst_p) = ntohl(*reinterpret_cast<const uint32_t*>(src_p));
    }


    std::string hostname_;
    int32_t port_;
    int32_t socket_fd_;
    char wbuf_[MAX_SINGLE_MESSAGE_LEN];
    int32_t wlen_;
    char rbuf_[MAX_SINGLE_MESSAGE_LEN];
    int32_t rlen_;

    ZPClient(const ZPClient&);
    ZPClient& operator=(const ZPClient&);
};

#endif
