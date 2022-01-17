#pragma once

enum class DNSReplyCode
{
  NoError = 0,
  FormError = 1,
  ServerFailure = 2,
  NonExistentDomain = 3,
  NotImplemented = 4,
  Refused = 5,
  YXDomain = 6,
  YXRRSet = 7,
  NXRRSet = 8
};

class DNSServer
{
  public:
    DNSServer() { };
    void processNextRequest() {} ;
    void setErrorReplyCode(const DNSReplyCode &replyCode) {};
    bool start(const uint16_t &port,
              const char* domainName,
              const uint32_t &resolvedIP) {return false;}
    void stop() {};
};

