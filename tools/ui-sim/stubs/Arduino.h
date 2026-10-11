#pragma once
// Desktop stand-in for the Arduino core: a simulated clock the scenario advances by hand.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

extern unsigned long sim_millis;
inline unsigned long millis() { return sim_millis; }
void delay(unsigned long ms);   // advances the clock (and keeps the UI's vibration loop happy)
