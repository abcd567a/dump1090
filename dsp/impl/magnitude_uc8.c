#include <math.h>
#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdalign.h>
#include <inttypes.h>

#include "dsp/helpers/tables.h"

/* Convert UC8 values to unsigned 16-bit magnitudes */

void STARCH_IMPL(magnitude_uc8, lookup) (const uc8_t *in, uint16_t *out, unsigned len)
{
    const uint16_t * const mag_table = get_uc8_mag_table();

    const uc8_u16_t * restrict in_align = (const uc8_u16_t *) STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    unsigned len1 = len;
    while (len1--) {
        uint16_t mag = mag_table[in_align[0].u16];
        out_align[0] = mag;

        out_align += 1;
        in_align += 1;
    }
}

void STARCH_IMPL(magnitude_uc8, lookup_unroll_4) (const uc8_t *in, uint16_t *out, unsigned len)
{
    const uint16_t * const mag_table = get_uc8_mag_table();

    const uc8_u16_t * restrict in_align = (const uc8_u16_t *) STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    unsigned len4 = len >> 2;
    unsigned len1 = len & 3;

    while (len4--) {
        uint16_t mag0 = mag_table[in_align[0].u16];
        uint16_t mag1 = mag_table[in_align[1].u16];
        uint16_t mag2 = mag_table[in_align[2].u16];
        uint16_t mag3 = mag_table[in_align[3].u16];

        out_align[0] = mag0;
        out_align[1] = mag1;
        out_align[2] = mag2;
        out_align[3] = mag3;

        out_align += 4;
        in_align += 4;
    }

    while (len1--) {
        uint16_t mag = mag_table[in_align[0].u16];

        out_align[0] = mag;

        out_align += 1;
        in_align += 1;
    }
}

void STARCH_IMPL(magnitude_uc8, exact) (const uc8_t *in, uint16_t *out, unsigned len)
{
    const uc8_t * restrict in_align = STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    unsigned len1 = len;

    while (len1--) {
        float I = (in_align[0].I - 127.5);
        float Q = (in_align[0].Q - 127.5);

        float magsq = I * I + Q * Q;
        float mag = sqrtf(magsq) * 65535.0 / 127.5;
        if (mag > 65535.0)
            mag = 65535.0;

        out_align[0] = (uint16_t)mag;

        in_align += 1;
        out_align += 1;
    }
}

void STARCH_IMPL(magnitude_uc8, approx) (const uc8_t *in, uint16_t *out, unsigned len)
{
    const uc8_t * restrict in_align = STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    unsigned len1 = len;

    while (len1--) {
        float I = fabsf(in_align[0].I - 127.5);
        float Q = fabsf(in_align[0].Q - 127.5);

        float minval = (I < Q ? I : Q);
        float maxval = (I < Q ? Q : I);

        float approx;
        if (minval < 0.4142135 * maxval)
            approx = (0.99 * maxval + 0.197 * minval)  * 65535 / 127.5;
        else
            approx = (0.84 * maxval + 0.561 * minval)  * 65535 / 127.5;
        if (approx > 65535)
            approx = 65535;

        out_align[0] = (uint16_t)approx;

        in_align += 1;
        out_align += 1;
    }
}

#ifdef STARCH_FEATURE_NEON

#include <arm_neon.h>

void STARCH_IMPL_REQUIRES(magnitude_uc8, neon_approx, STARCH_FEATURE_NEON) (const uc8_t *in, uint16_t *out, unsigned len)
{
    const uint8_t * restrict in_align = (const uint8_t *) STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    const uint16x8_t offset = vdupq_n_u16((uint16_t) (127.5 * 256));

    const int16x4_t constants0 = {
        (int16_t)(0.99 * 32768),
        (int16_t)(0.197 * 32768),
        (int16_t)(0.84 * 32768),
        (int16_t)(0.561 * 32768),
    };
    const unsigned constants0_lane_99 = 0;
    const unsigned constants0_lane_197 = 1;
    const unsigned constants0_lane_84 = 2;
    const unsigned constants0_lane_561 = 3;

    const int16x4_t constants1 = {
        (int16_t)(0.4142135 * 32768),
        0,
        0,
        0
    };
    const unsigned constants1_lane_4142 = 0;

    unsigned len8 = len >> 3;
    while (len8--) {
        uint8x8x2_t iq = vld2_u8(in_align);

        // widen to 16 bits, convert to signed
        uint16x8_t i_u16 = vshll_n_u8(iq.val[0], 8);
        uint16x8_t q_u16 = vshll_n_u8(iq.val[1], 8);
        int16x8_t i_s16 = vreinterpretq_s16_u16(vsubq_u16(i_u16, offset));
        int16x8_t q_s16 = vreinterpretq_s16_u16(vsubq_u16(q_u16, offset));

        // minval = min(|I|, |Q|)
        // maxval = max(|I|, |Q|)
        int16x8_t absi = vabsq_s16(i_s16);
        int16x8_t absq = vabsq_s16(q_s16);
        int16x8_t minval = vminq_s16(absi, absq);
        int16x8_t maxval = vmaxq_s16(absi, absq);

        // result =
        //   0.99*maxval + 0.197*minval   if minval < 0.4142135 * maxval
        //   0.84*maxval + 0.561*minval   if minval >= 0.4142135 * maxval
        int16x8_t threshold = vqdmulhq_lane_s16(maxval, constants1, constants1_lane_4142);
        int16x8_t lt_res = vqaddq_s16(vqrdmulhq_lane_s16(maxval, constants0, constants0_lane_99), vqrdmulhq_lane_s16(minval, constants0, constants0_lane_197));
        int16x8_t ge_res = vqaddq_s16(vqrdmulhq_lane_s16(maxval, constants0, constants0_lane_84), vqrdmulhq_lane_s16(minval, constants0, constants0_lane_561));
        uint16x8_t selector = vcgeq_s16(minval, threshold);
        uint16x8_t result = vreinterpretq_u16_s16(vbslq_s16(selector, ge_res, lt_res));
        uint16x8_t result2 = vqshlq_n_u16(result, 1);

        vst1q_u16(out_align, result2);

        in_align += 16;
        out_align += 8;
    }

    unsigned len1 = len & 7;
    while (len1--) {
        uint8x8x2_t iq = vld2_dup_u8(in_align);

        // widen to 16 bits, convert to signed
        uint16x4_t i_u16 = vget_low_u16(vshll_n_u8(iq.val[0], 8));
        uint16x4_t q_u16 = vget_low_u16(vshll_n_u8(iq.val[1], 8));
        int16x4_t i_s16 = vreinterpret_s16_u16(vsub_u16(i_u16, vget_low_u16(offset)));
        int16x4_t q_s16 = vreinterpret_s16_u16(vsub_u16(q_u16, vget_low_u16(offset)));

        // minval = min(|I|, |Q|)
        // maxval = max(|I|, |Q|)
        int16x4_t absi = vabs_s16(i_s16);
        int16x4_t absq = vabs_s16(q_s16);
        int16x4_t minval = vmin_s16(absi, absq);
        int16x4_t maxval = vmax_s16(absi, absq);

        // result =
        //   0.99*maxval + 0.197*minval   if minval < 0.4142135 * maxval
        //   0.84*maxval + 0.561*minval   if minval >= 0.4142135 * maxval
        int16x4_t threshold = vqdmulh_lane_s16(maxval, constants1, constants1_lane_4142);
        int16x4_t lt_res = vqadd_s16(vqrdmulh_lane_s16(maxval, constants0, constants0_lane_99), vqrdmulh_lane_s16(minval, constants0, constants0_lane_197));
        int16x4_t ge_res = vqadd_s16(vqrdmulh_lane_s16(maxval, constants0, constants0_lane_84), vqrdmulh_lane_s16(minval, constants0, constants0_lane_561));
        uint16x4_t selector = vcge_s16(minval, threshold);
        uint16x4_t result = vreinterpret_u16_s16(vbsl_s16(selector, ge_res, lt_res));
        uint16x4_t result2 = vqshl_n_u16(result, 1);

        // store 1 lane only
        vst1_lane_u16(out_align, result2, 0);

        in_align += 2;
        out_align += 1;
    }
}

#endif /* STARCH_FEATURE_NEON */
