#include "dump1090.h"

#define DEMOD_FN demodulateMulti2000
#define DEMOD_SAMPLES_PER_HALFBIT 1
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti4000
#define DEMOD_SAMPLES_PER_HALFBIT 2
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti6000
#define DEMOD_SAMPLES_PER_HALFBIT 3
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti8000
#define DEMOD_SAMPLES_PER_HALFBIT 4
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti10000
#define DEMOD_SAMPLES_PER_HALFBIT 5
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti12000
#define DEMOD_SAMPLES_PER_HALFBIT 6
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

void demodulateMulti(struct mag_buf *mag)
{
    if (Modes.sample_rate == 2000000)
        demodulateMulti2000(mag);
    else if (Modes.sample_rate == 2400000)
        demodulate2400(mag); /* old demod */
    else if (Modes.sample_rate == 4000000)
        demodulateMulti4000(mag);
    else if (Modes.sample_rate == 6000000)
        demodulateMulti6000(mag);
    else if (Modes.sample_rate == 8000000)
        demodulateMulti8000(mag);
    else if (Modes.sample_rate == 10000000)
        demodulateMulti10000(mag);
    else if (Modes.sample_rate == 12000000)
        demodulateMulti12000(mag);
    else
        abort();
}

void demodulateMultiAC(struct mag_buf *mag)
{
    if (Modes.sample_rate == 2400000)
        demodulate2400(mag); /* old demod */

    /* no support for A/C with multi-rate demod yet */
}
