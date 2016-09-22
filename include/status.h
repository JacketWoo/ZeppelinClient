#ifndef STATUS_H
#define STATUS_H

#include <string>

class Status {
  public:
    enum Code {
      kOk,
	  	kWait,
      kErr
    };

    Status(int32_t code = kOk, const std::string& msg = "") :
      code_(code),
      msg_(msg) {
    }

    virtual ~Status() {}

    bool ok() {
      return (code_ == kOk);
    }

    void SetOk(const std::string& msg = "") {
      code_ = kOk;
      msg_ = msg;
    }

    void SetErr(const std::string& msg = "") {
      code_ = kErr;
      msg_ = msg;
    }

		void SetWait(const std::string& msg = "") {
			code_ = kWait;
			msg_ = msg;
		}
		
		void Set(int32_t code, const std::string& msg = "") {
			code_ = code;
			msg_ = msg;
		}

    int32_t code() {
      return code_;
    }

    std::string msg() {
      return msg_;
    }
  private:
    int32_t code_;
    std::string msg_; 
};

#endif
