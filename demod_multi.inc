/* -*- c -*- */

static void DEMOD_FN (struct mag_buf *mag)
{
    /* precompute average of magnitude data */
    static uint16_t *averaged = NULL;
    static unsigned averaged_allocated = 0;
    const unsigned averaged_avail = mag->validLength - DEMOD_SAMPLES_PER_HALFBIT + 1;
    if (!averaged || averaged_allocated < averaged_avail) {
        free(averaged);
        averaged = malloc(averaged_avail * sizeof(uint16_t));
        averaged_allocated = averaged_avail;
    }
    starch_boxcar_u16(mag->data, mag->validLength, DEMOD_SAMPLES_PER_HALFBIT, averaged);

    /* precompute preamble correlation */
    static uint16_t *correlated = NULL;
    static unsigned correlated_allocated = 0;
    const unsigned correlated_avail = averaged_avail - DEMOD_SAMPLES_PER_HALFBIT * 9;
    if (!correlated || correlated_allocated < correlated_avail) {
        free(correlated);
        correlated = malloc(correlated_avail * sizeof(uint16_t));
        correlated_allocated = correlated_avail;
    }
    starch_preamble_u16(averaged, averaged_avail, DEMOD_SAMPLES_PER_HALFBIT, correlated);

    /* set threshold from mean of averaged data */
    double mean_level, mean_power;
    starch_mean_power_u16(averaged, averaged_avail, &mean_level, &mean_power);
    const uint32_t threshold = 65536.0 * mean_level * 2.0; /* 6dB above noise */

    const unsigned limit = mag->validLength - mag->overlap;

    assert(correlated_avail >= limit);

    /* Preamble pulse offsets, in samples */
    const unsigned P1 = 1 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned P2 = 3 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned P3 = 8 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned P4 = 10 * DEMOD_SAMPLES_PER_HALFBIT;
    /* Quiet period offsets, in samples */
    const unsigned Q1A = 0 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q1B = 2 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q2A = 2 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q2B = 4 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q3A = 7 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q3B = 9 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q4A = 9 * DEMOD_SAMPLES_PER_HALFBIT;
    const unsigned Q4B = 11 * DEMOD_SAMPLES_PER_HALFBIT;

    for (unsigned j = 0; j < limit; j++) {
        const uint16_t *sc = &correlated[j];
        const uint16_t *sa = &averaged[j];

        /* threshold detect against correlation score */
        if (sc[P1] < threshold)
            continue;

        /* verify pulse shapes */
        if (! (sa[Q1A] < sa[P1] &&
               sa[Q1B] < sa[P1] &&
               sa[Q2A] < sa[P2] &&
               sa[Q2B] < sa[P2] &&
               sa[Q3A] < sa[P3] &&
               sa[Q3B] < sa[P3] &&
               sa[Q4A] < sa[P4] &&
               sa[Q4B] < sa[P4]) )
            continue;

        /* find nearby correlation peak */
        unsigned best = 0, best_corr = 0;
        for (unsigned k = P1 - DEMOD_SAMPLES_PER_HALFBIT + 1; k < P1 + DEMOD_SAMPLES_PER_HALFBIT; ++k) {
            if (sc[k] > best_corr) {
                best = k;
                best_corr = sc[k];
            }
        }

        /* check at halfbit offset, should be substantially worse correlation */
        if (sc[best + DEMOD_SAMPLES_PER_HALFBIT] * 2 > sc[best])
            continue;

        /* exclude false positives where we have a strong preamble with
         * pulses 1/2 arriving 7 halfbits after our P1 correlation, i.e.:
         *
         * preamble correlator function:
         *
         *      __    __             __    __               X  bit  X
         *   __|  |__|  |___________|  |__|  |______________X   0   X
         *   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
         *   Q1A   Q1B            Q3A  Q3B                  | ---->
         *      P1    P2             P3    P4               | message
         *         Q2A  Q2B            Q4A   Q4B            | data
         *
         * incoming signal magnitude:
         *
         *   noise fools the         __    __  <-strong-> __    __
         *   pulse shape check ..   |  |  |  |   signal  |  |  |  |
         *   ___xxx___xxx___________|  |__|  |___________|  |__|  |__
         *   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
         *
         * -> weak correlation for a preamble starting at halfbit 1,
         *    strong correlation for a preamble starting at halfbit 8
         */
        if (sc[best + DEMOD_SAMPLES_PER_HALFBIT * 7] > sc[best] * 2)
            continue;

        /* good candidate, do the demodulation */
        Modes.stats_current.demod_preambles++;

        unsigned char rawbytes[MODES_LONG_MSG_BYTES];

        const uint16_t *data = &sa[best + DEMOD_SAMPLES_PER_HALFBIT * 16];
        for (unsigned byteNum = 0; byteNum < sizeof(rawbytes); ++byteNum) {
            unsigned value = 0;
            for (unsigned bitNum = 0; bitNum < 8; ++bitNum) {
                value <<= 1;
                if (data[0] > data[DEMOD_SAMPLES_PER_HALFBIT])
                    value |= 1;
                data += DEMOD_SAMPLES_PER_HALFBIT * 2;
            }
            rawbytes[byteNum] = value;
        }

        int score = scoreModesMessage(rawbytes, MODES_LONG_MSG_BITS);
        if (score < 0) {
            if (score == -1)
                Modes.stats_current.demod_rejected_unknown_icao++;
            else
                Modes.stats_current.demod_rejected_bad++;
            j += DEMOD_SAMPLES_PER_HALFBIT;
            continue; // no good
        }

        /* really demodulate it */
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));

        mm.timestampMsg = mag->sampleTimestamp + (j + best) * (6 / DEMOD_SAMPLES_PER_HALFBIT) + (8 + 56) * 12;
        mm.sysTimestampMsg = mag->sysTimestamp + receiveclock_ms_elapsed(mag->sampleTimestamp, mm.timestampMsg);
        mm.score = score;

        int result = decodeModesMessage(&mm, rawbytes);
        if (result < 0) {
            if (result == -1)
                Modes.stats_current.demod_rejected_unknown_icao++;
            else
                Modes.stats_current.demod_rejected_bad++;
            j += DEMOD_SAMPLES_PER_HALFBIT;
            continue;
        }

        Modes.stats_current.demod_accepted[mm.correctedbits]++;
        useModesMessage(&mm);

        /* skip to next message (ish) */
        j += 16 * DEMOD_SAMPLES_PER_HALFBIT              /* preamble */
            + 2 * DEMOD_SAMPLES_PER_HALFBIT * mm.msgbits /* data */
            - 8 * DEMOD_SAMPLES_PER_HALFBIT;             /* back up a bit, sometimes we can handle preambles that overlap the previous message */
    }
}
