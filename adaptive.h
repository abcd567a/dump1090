#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include <inttypes.h>

struct modesMessage;

void adaptive_init();
void adaptive_update(uint16_t *buf, unsigned length, struct modesMessage *decoded);

#endif
