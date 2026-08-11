#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include "stmlib/stmlib.h"

namespace plaits {

class UserData {
 public:
  enum {
    ADDRESS = 0x08007000,
    SIZE = 0x1000
  };

  UserData() { }
  ~UserData() { }

  inline const uint8_t* ptr(int slot) const {
    return NULL;
  }
  
  inline bool Save(uint8_t* rx_buffer, int slot) {
    return false;
  }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_
