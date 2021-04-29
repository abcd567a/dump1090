#ifndef AUTOGAIN_H
#define AUTOGAIN_H

#include <inttypes.h>

struct modesMessage;

void autogain_init();
void autogain_update(uint16_t *buf, unsigned length, struct modesMessage *decoded);

#endif
