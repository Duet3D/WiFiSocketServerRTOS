#include <cstring>
#include <algorithm>

#include "Config.h"
#include "DNSServer.h"
#include <lwip/def.h>

void replace(std::string &data, std::string to_replace, std::string replacement)
{
    size_t pos = data.find(to_replace);
    while(pos != std::string::npos)
    {
        data.replace(pos, to_replace.size(), replacement);
        pos =data.find(to_replace, pos + replacement.size());
    }
}

DNSServer::DNSServer()
{
  _ttl = htonl(60);
  _errorReplyCode = DNSReplyCode::NonExistentDomain;
}

bool DNSServer::start(const uint16_t &port, const std::string &domainName,
                     const ip_addr_t &resolvedIP)
{
  _port = port;
  _buffer = NULL;
  _domainName = domainName;
  unsigned char* resolvedIPAddr = (unsigned char*) &resolvedIP.u_addr.ip4.addr;
  _resolvedIP[0] = resolvedIPAddr[0];
  _resolvedIP[1] = resolvedIPAddr[1];
  _resolvedIP[2] = resolvedIPAddr[2];
  _resolvedIP[3] = resolvedIPAddr[3];
  downcaseAndRemoveWwwPrefix(_domainName);

  struct netconn* temp = netconn_new(NETCONN_UDP);

  if (temp)
  {
    netconn_set_nonblocking(temp, 1);
    err_t rc = netconn_bind(temp, IP4_ADDR_ANY, _port);
    if (rc == ERR_OK) {
      _udp = temp;
      return 1;
    }
    netconn_close(temp);
    netconn_delete(temp);
    _udp = nullptr;
  }

  return 0;
}

void DNSServer::setErrorReplyCode(const DNSReplyCode &replyCode)
{
  _errorReplyCode = replyCode;
}

void DNSServer::setTTL(const uint32_t &ttl)
{
  _ttl = htonl(ttl);
}

void DNSServer::stop()
{
  netconn_close(_udp);
  netconn_delete(_udp);
  _udp = nullptr;
  free(_buffer);
  _buffer = NULL;
}

void DNSServer::downcaseAndRemoveWwwPrefix(std::string &domainName)
{
  std::for_each(domainName.begin(), domainName.end(), [](char & c){
      c = ::tolower(c);
  });
  replace(domainName, "www", "");
}

void DNSServer::processNextRequest()
{
  struct netbuf *data = nullptr;
  netconn_recv(_udp, &data);

  _currentPacketSize = data ? netbuf_len(data) : 0;

  if (_currentPacketSize > 0) 
  {
    _remotePort = netbuf_fromport(data);
    memcpy(&_remoteIp, netbuf_fromaddr(data), sizeof(_remoteIp));
    if (_buffer != NULL) free(_buffer);
    _buffer = (unsigned char*)malloc(_currentPacketSize * sizeof(char));
    if (_buffer == NULL) return;
    netbuf_copy(data, _buffer, _currentPacketSize);
    _dnsHeader = (DNSHeader*) _buffer;

    if (_dnsHeader->QR == DNS_QR_QUERY &&
        _dnsHeader->OPCode == DNS_OPCODE_QUERY &&
        requestIncludesOnlyOneQuestion() &&
        (_domainName == "*" || getDomainNameWithoutWwwPrefix() == _domainName)
      )
    {
      replyWithIP();
    }
    else if (_dnsHeader->QR == DNS_QR_QUERY)
    {
      replyWithCustomCode();
    }

    free(_buffer);
    netbuf_delete(data);
    _buffer = NULL;
  }
}

bool DNSServer::requestIncludesOnlyOneQuestion()
{
  return ntohs(_dnsHeader->QDCount) == 1 &&
         _dnsHeader->ANCount == 0 &&
         _dnsHeader->NSCount == 0 &&
         _dnsHeader->ARCount == 0;
}

std::string DNSServer::getDomainNameWithoutWwwPrefix()
{
  std::string parsedDomainName = "";
  if (_buffer == NULL) return parsedDomainName;
  unsigned char *start = _buffer + 12;
  if (*start == 0)
  {
    return parsedDomainName;
  }
  int pos = 0;
  while(true)
  {
    unsigned char labelLength = *(start + pos);
    for(int i = 0; i < labelLength; i++)
    {
      pos++;
      parsedDomainName += (char)*(start + pos);
    }
    pos++;
    if (*(start + pos) == 0)
    {
      downcaseAndRemoveWwwPrefix(parsedDomainName);
      return parsedDomainName;
    }
    else
    {
      parsedDomainName += ".";
    }
  }
}

void DNSServer::replyWithIP()
{
  if (_buffer == NULL) return;
  _dnsHeader->QR = DNS_QR_RESPONSE;
  _dnsHeader->ANCount = _dnsHeader->QDCount;
  _dnsHeader->QDCount = _dnsHeader->QDCount; 
  // _dnsHeader->RA = 1;  

  struct netbuf* data = netbuf_new();
  uint8_t* allocd = (uint8_t*)netbuf_alloc(data, _currentPacketSize + 16);
  netbuf_take(data, _buffer, _currentPacketSize);

  uint8_t *more = &allocd[_currentPacketSize];

  more[0] = 192; //  answer name is a pointer
  more[1] = 12; // pointer to offset at 0x00c

  more[2] = 0; // 0x0001  answer is type A query (host address)
  more[3] = 1;

  more[4] = 0; //0x0001 answer is class IN (internet address)
  more[5] = 1;

  more[6] = ((uint8_t*)_ttl)[3];
  more[7] = ((uint8_t*)_ttl)[2];
  more[8] = ((uint8_t*)_ttl)[1];
  more[9] = ((uint8_t*)_ttl)[0];

  // Length of RData is 4 bytes (because, in this case, RData is IPv4)
  more[10] = 0;
  more[11] = 4;
  more[12] = _resolvedIP[3];
  more[13] = _resolvedIP[2];
  more[14] = _resolvedIP[1];
  more[15] = _resolvedIP[0];

  netconn_sendto(_udp, data, &_remoteIp, _remotePort);
  netbuf_delete(data);

  debugPrintf("DNS responds: %u.%u.%u.%u for %s\n", 
            _resolvedIP[0], _resolvedIP[1], _resolvedIP[2], _resolvedIP[3], 
            getDomainNameWithoutWwwPrefix().c_str());
}

void DNSServer::replyWithCustomCode()
{
  if (_buffer == NULL) return;
  _dnsHeader->QR = DNS_QR_RESPONSE;
  _dnsHeader->RCode = (unsigned char)_errorReplyCode;
  _dnsHeader->QDCount = 0;

  struct netbuf* data = netbuf_new();
  netbuf_alloc(data, sizeof(DNSHeader));
  netbuf_take(data, _buffer, sizeof(DNSHeader));
  netconn_sendto(_udp, data, &_remoteIp, _remotePort);
  netbuf_delete(data);
}