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
    DNSServer();
    void processNextRequest();
    void setErrorReplyCode(const DNSReplyCode &replyCode);
    // void setTTL(const uint32_t &ttl);

    // Returns true if successful, false if there are no sockets available
    bool start(const uint16_t &port,
              const char* domainName,
              const IPAddress &resolvedIP);
    // stops the DNS server
    void stop();

//   private:
//     WiFiUDP _udp;
//     uint16_t _port;
//     String _domainName;
//     unsigned char _resolvedIP[4];
//     int _currentPacketSize;
//     unsigned char* _buffer;
//     DNSHeader* _dnsHeader;
//     uint32_t _ttl;
//     DNSReplyCode _errorReplyCode;

//     void downcaseAndRemoveWwwPrefix(String &domainName);
//     String getDomainNameWithoutWwwPrefix();
//     bool requestIncludesOnlyOneQuestion();
//     void replyWithIP();
//     void replyWithCustomCode();
};

