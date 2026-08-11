#ifndef PLAITS_USER_DATA_RECEIVER_H_
#define PLAITS_USER_DATA_RECEIVER_H_

#include "stmlib/stmlib.h"
#include <cstddef>

namespace plaits {

enum PacketDecoderState {
    PACKET_DECODER_STATE_IDLE = 0,
    PACKET_DECODER_STATE_OK,
    PACKET_DECODER_STATE_ERROR
};

class UserDataReceiver {
 public:
  UserDataReceiver() { }
  ~UserDataReceiver() { }
  
  void Init(uint8_t* buffer, size_t size) {}
  PacketDecoderState Process(float sample) { return PACKET_DECODER_STATE_IDLE; }
  
  inline float progress() const { return 0.0f; }
  inline uint8_t* rx_buffer() { return nullptr; }
  void Reset() {}
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_RECEIVER_H_
