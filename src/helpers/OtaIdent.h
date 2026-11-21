#pragma once

#include <stdio.h>
#include <string.h>
#include <stddef.h>

/**
 * The repeater's LoRa OTA ident: "<version> <date> <commit>|<board>", at most cap-1 characters (60, what the
 * INFO reply carries). The app splits it at '|' and reads the commit as the last word before it, so the date,
 * the commit and the '|' always fit; what is left goes to the board (the PlatformIO env when it fits, else the
 * manufacturer name) and the version, which is cut from its start (its end tells most: "...-<sha>").
 * A version that ends with "-<commit>" (a branch build) is not repeated with the commit.
 */
inline void formatOtaIdent(char* out, size_t cap, const char* version, const char* date, const char* commit,
                           const char* env, const char* manufacturer) {
  if (cap < 2) { if (cap) out[0] = 0; return; }
  const size_t maxlen = cap - 1;
  size_t cl = strlen(commit), dl = strlen(date), vl = strlen(version);
  if (cl > 12) cl = 12;
  if (dl + cl + 2 > maxlen) dl = (maxlen > cl + 2) ? maxlen - cl - 2 : 0;
  // "<version>-<commit>": the commit is in the ident anyway
  if (vl > cl && version[vl - cl - 1] == '-' && strncmp(&version[vl - cl], commit, cl) == 0 && strlen(commit) == cl) vl -= cl + 1;
  const size_t vlFull = vl;                                  // the version without the commit
  const size_t fixed = dl + 1 + cl + 1;                      // "<date> <commit>|"
  const size_t avail = maxlen > fixed ? maxlen - fixed : 0;  // for the version and the board
  const size_t minver = vl < 7 ? vl : 7;
  const char* board = manufacturer;
  if (env && env[0] && strlen(env) + (minver ? 1 + minver : 0) <= avail) board = env;
  size_t bl = strlen(board);
  size_t vneed = minver ? 1 + minver : 0;                    // a board is cut before the version is
  if (bl + vneed > avail) bl = avail > vneed ? avail - vneed : 0;
  size_t vmax = avail > bl + 1 ? avail - bl - 1 : 0;         // the version and its space
  if (vl > vmax) vl = vmax;
  const char* vstart = version + (vlFull - vl);              // the end of the version is kept
  size_t n = 0;
  if (vl) n += snprintf(out + n, cap - n, "%.*s ", (int)vl, vstart);
  n += snprintf(out + n, cap - n, "%.*s %.*s|%.*s", (int)dl, date, (int)cl, commit, (int)bl, board);
}
