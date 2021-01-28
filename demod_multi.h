#ifndef DEMOD_MULTI_H
#define DEMOD_MULTI_H

struct mag_buf;

void demodulateMulti(struct mag_buf *mag);
void demodulateMultiAC(struct mag_buf *mag);

#endif
