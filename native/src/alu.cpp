#include "alu.h"
namespace lift {
uint8_t PAR[256];
namespace { struct Init { Init() { for (int i = 0; i < 256; i++) { int n = 0; for (int b = 0; b < 8; b++) n += (i >> b) & 1; PAR[i] = (n & 1) ? 0 : FP; } } } init; }
}
