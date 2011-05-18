extern "C" {
  #include "utility/wl_definitions.h"
  #include "utility/wl_types.h"
  #include "socket.h"
  #include "string.h"
  #include "utility/debug.h"
}

#include "WiFi.h"
#include "Client.h"
#include "Server.h"
#include "server_drv.h"

uint16_t Client::_srcport = 1024;

Client::Client() : _sock(MAX_SOCK_NUM) {
}

Client::Client(uint8_t sock) : _sock(sock) {
}

int Client::connect(const char* host, uint16_t port) {
	/* TODO Add DNS wifi spi function to resolve DNS */
#if 0
  // Look up the host first
  int ret = 0;
  DNSClient dns;
  IPAddress remote_addr;

  dns.begin(Ethernet.dnsServerIP());
  ret = dns.getHostByName(host, remote_addr);
  if (ret == 1) {
    return connect(remote_addr, port);
  } else {
    return ret;
  }
#endif
}

int Client::connect(IPAddress ip, uint16_t port) {
    _sock = getFirstSocket();
    if (_sock != NO_SOCKET_AVAIL)
    {
    	ServerDrv::StartClient(uint32_t(ip), port, _sock);
    	 WiFiClass::_state[_sock] = _sock;
    }else{
    	return 0;
    }
    return 1;
}

void Client::write(uint8_t b) {
  if (_sock != 255)
  {
	  START();
      ServerDrv::sendData(_sock, &b, 1);
      while (!ServerDrv::isDataSent(_sock));
      END();

  }
}

void Client::write(const char *str) {
  if (_sock != 255)
  {
      unsigned int len = strlen(str);
      ServerDrv::sendData(_sock, (const uint8_t *)str, len);
      while (!ServerDrv::isDataSent(_sock));
  }
}

void Client::write(const uint8_t *buf, size_t size) {
  if (_sock != 255)
  {
      ServerDrv::sendData(_sock, buf, size);
      while (!ServerDrv::isDataSent(_sock));
  }
  
}

int Client::available() {
  if (_sock != 255)
  {
      return ServerDrv::availData(_sock);
  }
   
  return 0;
}

int Client::read() {
  uint8_t b;
  if (!available())
    return -1;
  ServerDrv::getData(_sock, &b);
  return b;
}


int Client::read(uint8_t* buf, size_t size) {
  if (!ServerDrv::getDataBuf(_sock, buf, &size))
      return -1;
  return 0;
}

int Client::peek() {
	//TODO to be implemented
	return 0;
}

void Client::flush() {
  while (available())
    read();
}

void Client::stop() {
  if (_sock == 255)
    return;
  
  // attempt to close the connection gracefully (send a FIN to other side)
  disconnect(WiFiClass::_state[_sock]);
  unsigned long start = millis();
  
  // wait a second for the connection to close
  while (status() != CLOSED && millis() - start < 1000)
    delay(1);
    
  // if it hasn't closed, close it forcefully
  if (status() != CLOSED)
    close(_sock);
  
  WiFiClass::_server_port[_sock] = 0;
  _sock = 255;
}

uint8_t Client::connected() {
  if (_sock == 255) {
    return 0;
  } else {
    uint8_t s = status();
    return !(s == LISTEN || s == CLOSED || s == FIN_WAIT_1 || s == FIN_WAIT_2 ||
             (s == CLOSE_WAIT && !available()));
  }
}

uint8_t Client::status() {
    if (_sock == 255) {
    return CLOSED;
  } else {
    return ServerDrv::getState(_sock);
  }
}

Client::operator bool() {
  return _sock != 255;
}

// Private Methods
uint8_t Client::getFirstSocket()
{
    for (int i = 0; i < MAX_SOCK_NUM; i++) {
      if (WiFiClass::_state[i] == 0)
      {
          return i;
      }
    }
    return SOCK_NOT_AVAIL;
}

