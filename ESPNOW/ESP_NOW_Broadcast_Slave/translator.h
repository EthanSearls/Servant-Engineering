#ifndef TRANSLATOR_H
#define TRANSLATOR_H

const char* translateCommand(uint8_t code) {
  switch (code) {
    case 0: return "off";
    case 1: return "on";
    case 2: return "vol up";
    case 3: return "vol down";
    case 4: return "chan up";
    case 5: return "chan down";
    default: return "unknown";
  }
}

#endif
