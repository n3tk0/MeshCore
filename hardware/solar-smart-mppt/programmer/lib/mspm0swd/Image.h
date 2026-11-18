#pragma once
// Firmware image for the MSPM0: a flat binary from address 0, from a .bin or an Intel HEX file.
#include <stdint.h>
#include <stddef.h>

#define IMAGE_MAX      0xF000u   // the application area; 0xF000-0xF7FF holds the MSPM0 settings

// Intel HEX text -> flat image from 0 (gaps 0xFF). Returns the length, or -1 bad record,
// -2 checksum, -3 address outside 0..max, -4 nothing in it.
int hexToBin(const char* text, size_t text_len, uint8_t* out, uint32_t max);

// a plausible MSPM0 application: stack pointer in SRAM, reset vector in the image (Thumb bit set)
bool imageLooksValid(const uint8_t* img, uint32_t len);
