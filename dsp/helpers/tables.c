#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "dsp-types.h"
#include "dsp/helpers/tables.h"

const uint16_t * get_uc8_mag_table()
{
    static uint16_t *table = NULL;

    if (!table) {
        table = malloc(sizeof(uint16_t) * 256 * 256);
        if (!table) {
            fprintf(stderr, "can't allocate UC8 conversion lookup table\n");
            abort();
        }

        for (int i = 0; i <= 255; i++) {
            for (int q = 0; q <= 255; q++) {
                float fI, fQ, magsq;

                fI = (i - 127.5) / 127.5;
                fQ = (q - 127.5) / 127.5;
                magsq = fI * fI + fQ * fQ;
                if (magsq > 1)
                    magsq = 1;
                float mag = sqrtf(magsq);

                uc8_u16_t u;
                u.uc8.I = i;
                u.uc8.Q = q;
                table[u.u16] = round(mag * 65535.0f);
            }
        }
    }

    return table;
}

const uint16_t * get_sc16q11_mag_11bit_table()
{
    static uint16_t *table = NULL;

    if (!table) {
        table = malloc(sizeof(uint16_t) * 2048 * 2048);
        if (!table) {
            fprintf(stderr, "can't allocate SC16Q11 conversion lookup table\n");
            abort();
        }

        for (int i = 0; i <= 2047; i++) {
            for (int q = 0; q <= 2047; q++) {
                float fI, fQ, magsq;

                fI = i;
                fQ = q;
                magsq = fI * fI + fQ * fQ;

                float mag = sqrtf(magsq) * 65536 / 2048;
                if (mag > 65535.0)
                    mag = 65535.0;

                table[(q << 11) | i] = (uint16_t) (mag + 0.5f);
            }
        }
    }

    return table;
}

