#include "blazepod_protocol.h"


namespace blazepod {

bool buildAuthPayload(const uint8_t* mfr, size_t len, uint8_t out[kAuthPayloadLen]) {
  if (mfr == nullptr || len < kMinMfrDataLen) return false;

  // Port of sasodoma/blazepod-hacking main.c. Keep this exactly as the
  // reference: all arithmetic is uint32_t with natural wraparound.
  const uint8_t* tail = mfr + (len - 5);
  const uint8_t offset = tail[0];
  const uint8_t* byteArray = tail + 1;  // 4 bytes

  uint32_t poly = 0xEDB88321u + (offset % 50);

  uint32_t crc = 0xFFFFFFFFu;
  for (int i = 0; i < 4; i++) {
    crc ^= byteArray[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ poly;
      } else {
        crc = (crc >> 1);
      }
    }
  }

  crc = crc ^ 0xFFFFFFFFu;

  crc = crc + (crc << 3);
  crc = crc ^ (crc >> 11);
  crc = crc + (crc << 15);

  out[0] = 0x73;  // 's'
  out[1] = 0x65;  // 'e'
  out[2] = 0x61;  // 'a'
  out[3] = (uint8_t)(crc);
  out[4] = (uint8_t)(crc >> 8);
  out[5] = (uint8_t)(crc >> 16);
  out[6] = (uint8_t)(crc >> 24);
  return true;
}

size_t encodeColour(uint8_t r, uint8_t g, uint8_t b, bool offOnTap, uint8_t out[4]) {
  out[0] = g;
  out[1] = b;
  out[2] = r;
  if (offOnTap) {
    out[3] = 0x01;
    return 4;
  }
  return 3;
}

TapParse parseTap(const uint8_t* data, size_t len, Tap& out) {
  if (data == nullptr || len < 5) return TapParse::TooShort;
  switch (data[0]) {
    case 0x00: out.state = TapState::WhileOff; break;
    case 0x21: out.state = TapState::LitStayedOn; break;
    case 0x25: out.state = TapState::LitTurnedOff; break;
    default: return TapParse::UnknownState;
  }
  out.elapsedMs = (uint32_t)data[1] | ((uint32_t)data[2] << 8) | ((uint32_t)data[3] << 16) |
                  ((uint32_t)data[4] << 24);
  return TapParse::Ok;
}

bool looksLikePod(const char* name, size_t mfrLen, bool advertisesPodService) {
  if (mfrLen < kMinMfrDataLen) return false;
  if (advertisesPodService) return true;
  if (name == nullptr) return false;
  for (size_t i = 0; kNamePrefix[i]; i++) {
    char a = name[i], b = kNamePrefix[i];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;  // also stops at the end of a shorter name
  }
  return true;
}

}  // namespace blazepod
