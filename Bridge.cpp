/*
  Copyright (c) 2013 Arduino LLC. All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "Bridge.h"

BridgeClass::BridgeClass(Stream &_stream) : index(0), stream(_stream), started(false) {
  // Empty
}

void BridgeClass::begin() {
  if (started)
    return;
  started = true;
  
  // Wait for Atheros bootloader to finish startup
  do {
    dropAll();
    delay(1100);
  } while (stream.available()>0);
  
  // Bridge startup:
  // - If the bridge is not running starts it safely
  stream.print(CTRL_C);
  delay(250);
  stream.print(F("\n"));
  delay(500);
  stream.print(F("\n"));
  delay(750);
  // Wait for OpenWRT message
  // "Press enter to activate console"
  stream.print(F("run-bridge\n"));
  delay(500);
  dropAll();
  
  // - If the bridge was already running previous commands
  //   are ignored as "invalid packets".
  
  // Reset the brigde
  uint8_t cmd[] = {'X','X', '1','0','0'};
  uint8_t res[1];
  transfer(cmd, 5, res, 1);
  if (res[0] != 0)
	while (true);
}

void BridgeClass::put(const char *key, const char *value) {
  // TODO: do it in a more efficient way
  String cmd = "D";
  cmd += key;
  cmd += "\xFE";
  cmd += value;
  transfer((uint8_t*)cmd.c_str(), cmd.length());
}

unsigned int BridgeClass::get(const char *key, uint8_t *value, unsigned int maxlen) {
  uint8_t cmd[] = {'d'};
  unsigned int l = transfer(cmd, 1, (uint8_t *)key, strlen(key), value, maxlen);
  if (l < maxlen)
    value[l] = 0; // Zero-terminate string
  return l;
}

void BridgeClass::crcUpdate(uint8_t c) {
  CRC = CRC ^ c;
  CRC = (CRC >> 8) + (CRC << 8);
}

void BridgeClass::crcReset() {
  CRC = 0xAAAA;
}

void BridgeClass::crcWrite() {
  stream.write((char)(CRC >> 8));
  stream.write((char)(CRC & 0xFF));
}

bool BridgeClass::crcCheck(uint16_t _CRC) {
  return CRC == _CRC;
}

uint16_t BridgeClass::transfer(const uint8_t *buff1, uint16_t len1,
                 const uint8_t *buff2, uint16_t len2,
                 const uint8_t *buff3, uint16_t len3,
                 uint8_t *rxbuff, uint16_t rxlen)
{
  uint16_t len = len1 + len2 + len3;
  for ( ; ; delay(100), dropAll() /* Delay for retransmission */) {
    // Send packet
    crcReset();
    stream.write((char)0xFF);                // Start of packet (0xFF)
    crcUpdate(0xFF);
    stream.write((char)index);               // Message index
    crcUpdate(index);
    stream.write((char)((len >> 8) & 0xFF)); // Message length (hi)
    crcUpdate((len >> 8) & 0xFF);
    stream.write((char)(len & 0xFF));        // Message length (lo)
    crcUpdate(len & 0xFF);
    for (uint16_t i=0; i<len1; i++) {  // Payload
      stream.write((char)buff1[i]);
      crcUpdate(buff1[i]);
    }
    for (uint16_t i=0; i<len2; i++) {  // Payload
      stream.write((char)buff2[i]);
      crcUpdate(buff2[i]);
    }
    for (uint16_t i=0; i<len3; i++) {  // Payload
      stream.write((char)buff3[i]);
      crcUpdate(buff3[i]);
    }
    crcWrite();                     // CRC
  
    // Wait for ACK in 100ms
    if (timedRead(100) != 0xFF)
      continue;
    crcReset();
    crcUpdate(0xFF);
    
    // Check packet index
    if (timedRead(5) != index)
      continue;
    crcUpdate(index);
    
    // Recv len
    int lh = timedRead(5);
    if (lh < 0)
      continue;
    crcUpdate(lh);
    int ll = timedRead(5);
    if (ll < 0)
      continue;
    crcUpdate(ll);
    uint16_t l = lh;
    l <<= 8;
    l += ll;

    // Recv data
    for (uint16_t i=0; i<l; i++) {
      int c = timedRead(5);
      if (c < 0)
        continue;
      // Cut received data if rxbuffer is too small
      if (i < rxlen)
        rxbuff[i] = c;
      crcUpdate(c);
    }
    
    // Check CRC
    int crc_hi = timedRead(5);
    if (crc_hi < 0)
      continue;
    int crc_lo = timedRead(5);
    if (crc_lo < 0)
      continue;
    if (!crcCheck((crc_hi<<8)+crc_lo))
      continue;
    
    // Increase index
    index++;
    
    // Return bytes received
    if (l > rxlen)
      return rxlen;
    return l;
  }
}

int BridgeClass::timedRead(unsigned int timeout) {
  int c;
  unsigned long _startMillis = millis();
  do {
    c = stream.read();
    if (c >= 0) return c;
  } while(millis() - _startMillis < timeout);
  return -1;     // -1 indicates timeout
}

void BridgeClass::dropAll() {
  while (stream.available() > 0) {
    stream.read();
  }
}

// Bridge instance
#ifdef __AVR_ATmega32U4__
  // Leonardo variants (where HardwareSerial is Serial1)
  SerialBridgeClass Bridge(Serial1);
#else
  SerialBridgeClass Bridge(Serial);
#endif
