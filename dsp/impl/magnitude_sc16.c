#include <math.h>
#include <endian.h>

/* Convert (little-endian) SC16 values to unsigned 16-bit magnitudes */

void STARCH_IMPL(magnitude_sc16, exact_u32) (const sc16_t *in, uint16_t *out, unsigned len)
{
    const sc16_t * restrict in_align = STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    while (len--) {
        uint32_t I = abs((int16_t) le16toh(in_align[0].I));
        uint32_t Q = abs((int16_t) le16toh(in_align[0].Q));

        uint32_t magsq = I * I + Q * Q;
        float mag = sqrtf(magsq) * 2;
        if (mag > 65535.0)
            mag = 65535.0;
        out_align[0] = (uint16_t)mag;

        out_align += 1;
        in_align += 1;
    }
}

void STARCH_IMPL(magnitude_sc16, exact_float) (const sc16_t *in, uint16_t *out, unsigned len)
{
    const sc16_t * restrict in_align = STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    while (len--) {
        float I = abs((int16_t) le16toh(in_align[0].I)) * 2;
        float Q = abs((int16_t) le16toh(in_align[0].Q)) * 2;

        float magsq = I * I + Q * Q;
        float mag = sqrtf(magsq);
        if (mag > 65535.0)
            mag = 65535.0;
        out_align[0] = (uint16_t)mag;

        out_align += 1;
        in_align += 1;
    }
}

#ifdef STARCH_FEATURE_NEON

#include <arm_neon.h>

void STARCH_IMPL_REQUIRES(magnitude_sc16, neon_approx, STARCH_FEATURE_NEON) (const sc16_t *in, uint16_t *out, unsigned len)
{
    const int16_t * restrict in_align = (const int16_t *) STARCH_ALIGNED(in);
    uint16_t * restrict out_align = STARCH_ALIGNED(out);

    // This uses the same magnitude approximation as the "approx" variant above

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
        int16x8x2_t iq = vld2q_s16(in_align);
        int16x8_t i16 = iq.val[0];
        int16x8_t q16 = iq.val[1];

        // minval = min(|I|, |Q|)
        // maxval = max(|I|, |Q|)
        int16x8_t absi = vqabsq_s16(i16);
        int16x8_t absq = vqabsq_s16(q16);
        int16x8_t minval = vminq_s16(absi, absq);
        int16x8_t maxval = vmaxq_s16(absi, absq);

        // result =
        //   0.99*maxval + 0.197*minval   if minval < 0.4142135 * maxval
        //   0.84*maxval + 0.561*minval   if minval >= 0.4142135 * maxval
        int16x8_t threshold = vqrdmulhq_lane_s16(maxval, constants1, constants1_lane_4142);
        uint16x8_t selector = vcgeq_s16(minval, threshold);
        int16x8_t lt_res = vqaddq_s16(vqrdmulhq_lane_s16(maxval, constants0, constants0_lane_99), vqrdmulhq_lane_s16(minval, constants0, constants0_lane_197));
        int16x8_t ge_res = vqaddq_s16(vqrdmulhq_lane_s16(maxval, constants0, constants0_lane_84), vqrdmulhq_lane_s16(minval, constants0, constants0_lane_561));
        uint16x8_t result = vreinterpretq_u16_s16(vbslq_s16(selector, ge_res, lt_res));
        uint16x8_t result2 = vshlq_n_u16(result, 1);

        vst1q_u16(out_align, result2);

        in_align += 16;
        out_align += 8;
    }

    unsigned len1 = len & 7;
    while (len1--) {
        int16x4x2_t iq = vld2_dup_s16(in_align);
        int16x4_t i16 = iq.val[0];
        int16x4_t q16 = iq.val[1];

        // minval = min(|I|, |Q|)
        // maxval = max(|I|, |Q|)
        int16x4_t absi = vqabs_s16(i16);
        int16x4_t absq = vqabs_s16(q16);
        int16x4_t minval = vmin_s16(absi, absq);
        int16x4_t maxval = vmax_s16(absi, absq);

        // result =
        //   0.99*maxval + 0.197*minval   if minval < 0.4142135 * maxval
        //   0.84*maxval + 0.561*minval   if minval >= 0.4142135 * maxval
        int16x4_t threshold = vqrdmulh_lane_s16(maxval, constants1, constants1_lane_4142);
        uint16x4_t selector = vcge_s16(minval, threshold);
        int16x4_t lt_res = vqadd_s16(vqrdmulh_lane_s16(maxval, constants0, constants0_lane_99), vqrdmulh_lane_s16(minval, constants0, constants0_lane_197));
        int16x4_t ge_res = vqadd_s16(vqrdmulh_lane_s16(maxval, constants0, constants0_lane_84), vqrdmulh_lane_s16(minval, constants0, constants0_lane_561));
        uint16x4_t result = vreinterpret_u16_s16(vbsl_s16(selector, ge_res, lt_res));
        uint16x4_t result2 = vshl_n_u16(result, 1);

        vst1_lane_u16(out_align, result2, 0);

        in_align += 2;
        out_align += 1;
    }
}

#endif /* STARCH_FEATURE_NEON */
