// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// adaptive.c: adaptive gain control
//
// Copyright (c) 2021 FlightAware, LLC
//
// This file is free software: you may copy, redistribute and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your
// option) any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "dump1090.h"
#include "adaptive.h"

//
// gain limits
//
static int adaptive_gain_min;
static int adaptive_gain_max;

// gain steps relative to current gain
static float adaptive_gain_up_db;
static float adaptive_gain_down_db;

//
// block handling
//

static unsigned adaptive_block_remaining;
static unsigned adaptive_block_size;

void adaptive_init();
void adaptive_update(uint16_t *buf, unsigned length, struct modesMessage *decoded);
static void adaptive_update_single(uint16_t *buf, unsigned length, struct modesMessage *decoded);
static void adaptive_end_of_block();

//
// burst handling
//

static unsigned adaptive_burst_window_size;
static unsigned adaptive_burst_window_remaining;
static unsigned adaptive_burst_window_counter;
static unsigned adaptive_burst_runlength;
static unsigned adaptive_burst_block_counter;
static unsigned adaptive_burst_block_loud_decodes;
static double adaptive_burst_smoothed;
static double adaptive_burst_loud_decodes_smoothed;
static unsigned adaptive_burst_change_delay;
static double adaptive_burst_loud_threshold;

static void adaptive_burst_update(uint16_t *buf, unsigned length);
static void adaptive_burst_skip(unsigned length);
static unsigned adaptive_burst_count_samples(uint16_t *buf, unsigned n);
static void adaptive_burst_scan_windows(uint16_t *buf, unsigned windows);
static void adaptive_burst_end_of_window(unsigned counter);
static void adaptive_burst_end_of_block();

static void adaptive_burst_control_update();

//
// noise floor measurement (adaptive dynamic range)
//

static unsigned *adaptive_range_radix;
static unsigned adaptive_range_counter;
static double adaptive_range_smoothed;
static enum { RANGE_SCAN_IDLE, RANGE_SCAN_UP, RANGE_SCAN_DOWN } adaptive_range_state = RANGE_SCAN_UP;
static unsigned adaptive_range_delay;

static void adaptive_range_update(uint16_t *buf, unsigned length);
static void adaptive_range_end_of_block();
static void adaptive_range_control_update();


static bool adaptive_set_gain(int step, const char *why)
{
    if (step < adaptive_gain_min)
        step = adaptive_gain_min;
    if (step > adaptive_gain_max)
        step = adaptive_gain_max;

    int current_gain = sdrGetGain();
    if (current_gain == step)
        return false;

    fprintf(stderr, "adaptive: changing gain from %.1fdB (step %d) to %.1fdB (step %d) because: %s\n",
            sdrGetGainDb(current_gain), current_gain, sdrGetGainDb(step), step, why);

    int new_gain = sdrSetGain(step);
    return (current_gain != new_gain);
}

static void adaptive_gain_changed()
{
    int new_gain = sdrGetGain();
    adaptive_gain_up_db = sdrGetGainDb(new_gain + 1) - sdrGetGainDb(new_gain);
    adaptive_gain_down_db = sdrGetGainDb(new_gain) - sdrGetGainDb(new_gain - 1);
    
    double loud_threshold_dbfs = 0 - adaptive_gain_up_db - 3.0;
    adaptive_burst_loud_threshold = pow(10, loud_threshold_dbfs / 10.0);
}

void adaptive_init()
{
    int maxgain = sdrGetMaxGain();

    // If the SDR doesn't support gain control, disable ourselves
    if (maxgain < 0) {
        if (Modes.adaptive_burst_control || Modes.adaptive_range_control) {
            fprintf(stderr, "warning: adaptive gain control requested, but SDR gain control not available, ignored.\n");
        }
        Modes.adaptive_burst_control = false;
        Modes.adaptive_range_control = false;
    }        

    // If we're disabled, do nothing
    if (!Modes.adaptive_burst_control && !Modes.adaptive_range_control)
        return;

    // Look for 40us bursts
    adaptive_burst_window_size = Modes.sample_rate / 25000;
    adaptive_burst_window_remaining = adaptive_burst_window_size;
    adaptive_burst_window_counter = 0;
    adaptive_burst_change_delay = Modes.adaptive_burst_change_delay;    

    // Use an overall block size that is an exact multiple of the burst window, close to 1 second long
    adaptive_block_size = adaptive_burst_window_size * 25000;
    adaptive_block_remaining = adaptive_block_size;

    adaptive_range_radix = calloc(sizeof(unsigned), 65536);

    adaptive_range_state = RANGE_SCAN_UP;
    adaptive_range_delay = Modes.adaptive_range_scan_delay;

    // select and enforce gain limits
    for (adaptive_gain_min = 0; adaptive_gain_min < maxgain; ++adaptive_gain_min) {
        if (sdrGetGainDb(adaptive_gain_min) >= Modes.adaptive_min_gain_db)
            break;
    }

    for (adaptive_gain_max = maxgain; adaptive_gain_max > adaptive_gain_min; --adaptive_gain_max) {
        if (sdrGetGainDb(adaptive_gain_max) <= Modes.adaptive_max_gain_db)
            break;
    }

    fprintf(stderr, "adaptive: enabled adaptive gain control with gain limits %.1fdB (step %d) .. %.1fdB (step %d)\n",
            sdrGetGainDb(adaptive_gain_min), adaptive_gain_min, sdrGetGainDb(adaptive_gain_max), adaptive_gain_max);
    if (Modes.adaptive_range_control)
        fprintf(stderr, "adaptive: enabled dynamic range control, target dynamic range %.1fdB\n", Modes.adaptive_range_target);
    if (Modes.adaptive_burst_control)
        fprintf(stderr, "adaptive: enabled burst control\n");
    adaptive_set_gain(sdrGetGain(), "constraining gain to adaptive gain limits");
    adaptive_gain_changed();
}

// Feed some samples into the adaptive system. Any number of samples might be passed in.
void adaptive_update(uint16_t *buf, unsigned length, struct modesMessage *decoded)
{
    if (!Modes.adaptive_burst_control && !Modes.adaptive_range_control)
        return;
    
    // process samples up to a block boundary, then process the completed block
    while (length >= adaptive_block_remaining) {
        adaptive_update_single(buf, adaptive_block_remaining, decoded);
        buf += adaptive_block_remaining;
        length -= adaptive_block_remaining;

        adaptive_end_of_block();
        adaptive_block_remaining = adaptive_block_size;
    }

    // process final samples that don't complete a block
    if (length > 0) {
        adaptive_update_single(buf, length, decoded);
        adaptive_block_remaining -= length;
    }
}

// Feed some samples into the adaptive system. The samples are guaranteed to not cross a block boundary.
static void adaptive_update_single(uint16_t *buf, unsigned length, struct modesMessage *decoded)
{
    if (decoded) {
        if (/* decoded->msgbits == 112 && */ decoded->signalLevel >= adaptive_burst_loud_threshold)
            ++adaptive_burst_block_loud_decodes;
        adaptive_burst_skip(length);
    } else {
        adaptive_burst_update(buf, length);
        adaptive_range_update(buf, length);
    }
}

// Burst measurement: ignore the next 'length' samples (they are a successfully decoded message)
static void adaptive_burst_skip(unsigned length)
{
    if (!Modes.adaptive_burst_control)
        return;

    // first window
    if (length < adaptive_burst_window_remaining) {
        // partial fill
        adaptive_burst_window_remaining -= length;
        return;
    }

    // skip remainder of first window, dispatch it
    adaptive_burst_end_of_window(adaptive_burst_window_counter);
    length -= adaptive_burst_window_remaining;

    // skip remaining windows, dispatch them
    unsigned windows = length / adaptive_burst_window_size;
    unsigned samples = windows * adaptive_burst_window_size;
    while (windows--)
        adaptive_burst_end_of_window(0);

    length -= samples;

    // final partial window
    adaptive_burst_window_counter = 0;
    adaptive_burst_window_remaining = adaptive_burst_window_size - length;
}

// Burst measurement: process 'length' samples from 'buf', look for loud bursts;
// the samples might cross burst window boundaries;
// the samples will not cross a block boundary.
static void adaptive_burst_update(uint16_t *buf, unsigned length)
{
    if (!Modes.adaptive_burst_control)
        return;

    // first window
    if (length < adaptive_burst_window_remaining) {
        // partial fill
        adaptive_burst_window_counter += adaptive_burst_count_samples(buf, length);
        adaptive_burst_window_remaining -= length;
        return;
    }

    // complete fill of first partial window
    unsigned n = adaptive_burst_window_remaining;
    unsigned counter = adaptive_burst_window_counter + adaptive_burst_count_samples(buf, n);
    adaptive_burst_end_of_window(counter);
    buf += n;
    length -= n;

    // remaining windows
    unsigned windows = length / adaptive_burst_window_size;
    unsigned samples = windows * adaptive_burst_window_size;
    adaptive_burst_scan_windows(buf, windows);
    buf += samples;
    length -= samples;

    // final partial window
    adaptive_burst_window_counter = adaptive_burst_count_samples(buf, length);
    adaptive_burst_window_remaining = adaptive_burst_window_size - length;
}

// Burst measurement: process 'windows' complete burst windows starting at 'buf';
// 'buf' is aligned to the start of a burst window
static void adaptive_burst_scan_windows(uint16_t *buf, unsigned windows)
{
    while (windows--) {
        unsigned counter = adaptive_burst_count_samples(buf, adaptive_burst_window_size);
        buf += adaptive_burst_window_size;
        adaptive_burst_end_of_window(counter);
    }
}

// Burst measurement: process 'n' samples from 'buf', look for loud samples;
// the samples are guaranteed not to cross window boundaries;
// return the number of loud samples seen
static inline unsigned adaptive_burst_count_samples(uint16_t *buf, unsigned n)
{
    unsigned counter = 0;
    while (n--) {
        if (buf[0] > 46395) // -3dBFS
            ++counter;
        ++buf;
    }
    return counter;
}

// Burst measurement: we reached the end of a burst window with 'counter'
// loud samples seen, handle that window.
static void adaptive_burst_end_of_window(unsigned counter)
{
    if (counter > adaptive_burst_window_size / 4) {
        // This window is loud, extend any existing run of loud windows
        ++adaptive_burst_runlength;
    } else {
        // Quiet window. If we saw a run of loud windows >= 80us long, count
        // that as a candidate for an over-amplified message that was
        // not decoded.
        if (adaptive_burst_runlength >= 2 && adaptive_burst_runlength <= 5)
            ++adaptive_burst_block_counter;
        adaptive_burst_runlength = 0;
    }
}

// Noise measurement: process 'length' samples from 'buf'.
// The samples will not cross a block boundary.
static void adaptive_range_update(uint16_t *buf, unsigned length)
{
    if (!Modes.adaptive_range_control)
        return;

    adaptive_range_counter += length;
    while (length--) {
        // do a very simple radix sort of sample magnitudes
        // so we can later find the Nth percentile value
        ++adaptive_range_radix[buf[0]];
        ++buf;
    }
}

// Noise measurement: we reached the end of a block, update
// our noise estimate
static void adaptive_range_end_of_block()
{
    if (!Modes.adaptive_range_control)
        return;

    unsigned n = 0, i = 0;

    // measure Nth percentile magnitude
    unsigned count_n = adaptive_range_counter * Modes.adaptive_range_percentile / 100;
    while (i < 65536 && n <= count_n)
        n += adaptive_range_radix[i++];
    uint16_t percentile_n = i - 1;

    // maintain an EMA of the Nth percentile
    adaptive_range_smoothed = adaptive_range_smoothed * (1 - Modes.adaptive_range_alpha) + percentile_n * Modes.adaptive_range_alpha;
    // .. report to stats in dBFS
    if (adaptive_range_smoothed > 0) {
        Modes.stats_current.adaptive_noise_dbfs = 20 * log10(adaptive_range_smoothed / 65536.0);
    } else {
        Modes.stats_current.adaptive_noise_dbfs = 0;
    }

    // reset radix sort for the next block
    memset(adaptive_range_radix, 0, 65536 * sizeof(unsigned));
    adaptive_range_counter = 0;
}

// Burst measurement: we reached the end of a block, update our burst rate estimate
static void adaptive_burst_end_of_block()
{
    if (!Modes.adaptive_burst_control)
        return;

    // maintain an EMA of the number of bursts seen per block
    Modes.stats_current.adaptive_loud_undecoded += adaptive_burst_block_counter;
    adaptive_burst_smoothed = adaptive_burst_smoothed * (1 - Modes.adaptive_burst_alpha) + adaptive_burst_block_counter * Modes.adaptive_burst_alpha;
    adaptive_burst_block_counter = 0;

    // maintain an EMA of the number of decoded, but loud, messages seen per block
    Modes.stats_current.adaptive_loud_decoded += adaptive_burst_block_loud_decodes;
    adaptive_burst_loud_decodes_smoothed = adaptive_burst_loud_decodes_smoothed * (1 - Modes.adaptive_burst_alpha) + adaptive_burst_block_loud_decodes * Modes.adaptive_burst_alpha;
    adaptive_burst_block_loud_decodes = 0;
}

// consecutive blocks with loud rate
static unsigned adaptive_burst_loud_blocks = 0;
// consecutive blocks with quiet rate
static unsigned adaptive_burst_quiet_blocks = 0;
// are we suppressing gain due to a burst?
static bool adaptive_burst_suppressing = false;
// what was the gain before we started suppressing?
static int adaptive_burst_orig_gain = 0;

void flush_stats(uint64_t now);

static void adaptive_increase_gain(const char *why)
{
    if (adaptive_set_gain(sdrGetGain() + 1, why))
        adaptive_gain_changed();
}

static void adaptive_decrease_gain(const char *why)
{
    if (adaptive_set_gain(sdrGetGain() - 1, why))
        adaptive_gain_changed();
}

// Adaptive gain: we reached a block boundary. Update measurements and act on them.
static void adaptive_end_of_block()
{
    adaptive_range_end_of_block();
    adaptive_burst_end_of_block();

    adaptive_burst_control_update();
    adaptive_range_control_update();

    Modes.stats_current.adaptive_valid = true;
    unsigned current = Modes.stats_current.adaptive_gain = sdrGetGain();
    ++Modes.stats_current.adaptive_gain_seconds[current < STATS_GAIN_COUNT ? current : STATS_GAIN_COUNT-1];
    if (adaptive_burst_suppressing)
        ++Modes.stats_current.adaptive_gain_reduced_seconds;
}

static void adaptive_burst_control_update()
{
    if (!Modes.adaptive_burst_control)
        return;

    if (adaptive_range_state != RANGE_SCAN_IDLE)
        return;
    
    if (adaptive_burst_change_delay)
        --adaptive_burst_change_delay;

    if (!adaptive_burst_change_delay) {
        if (adaptive_burst_smoothed > Modes.adaptive_burst_loud_rate) {
            adaptive_burst_quiet_blocks = 0;
            ++adaptive_burst_loud_blocks;
        } else if (adaptive_burst_loud_decodes_smoothed < Modes.adaptive_burst_quiet_rate) {
            adaptive_burst_loud_blocks = 0;
            ++adaptive_burst_quiet_blocks;
        } else {
            adaptive_burst_loud_blocks = 0;
            adaptive_burst_quiet_blocks = 0;
        }

        if (adaptive_burst_loud_blocks >= Modes.adaptive_burst_loud_runlength) {
            // we need to reduce gain (further)
            if (!adaptive_burst_suppressing) {
                adaptive_burst_suppressing = true;
                adaptive_burst_orig_gain = sdrGetGain();
            }

            adaptive_decrease_gain("saw a noisy period with many undecoded loud messages");
            adaptive_burst_loud_blocks = 0;
            adaptive_burst_change_delay = Modes.adaptive_burst_change_delay;
        }

        if (adaptive_burst_suppressing && adaptive_burst_quiet_blocks >= Modes.adaptive_burst_quiet_runlength) {
            // we can relax the gain restriction
            adaptive_increase_gain("saw a quiet period with few loud messages");
            adaptive_burst_quiet_blocks = 0;
            adaptive_burst_change_delay = Modes.adaptive_burst_change_delay;

            if (sdrGetGain() >= adaptive_burst_orig_gain)
                adaptive_burst_suppressing = false;
        }
    }
}

static void adaptive_range_control_update()
{
    if (!Modes.adaptive_range_control)
        return;

    if (adaptive_range_delay > 0)
        --adaptive_range_delay;

    float available_range = -20 * log10(adaptive_range_smoothed / 65536.0);

    switch (adaptive_range_state) {
    case RANGE_SCAN_UP:
        if (adaptive_range_delay > 0)
            break;
        
        if (available_range < Modes.adaptive_range_target) {
            // Current gain fails to meet our target. Switch to downward scanning.
            fprintf(stderr, "adaptive: available dynamic range (%.1fdB) < required dynamic range (%.1fdB), switching to downward scan\n", available_range, Modes.adaptive_range_target);
            adaptive_decrease_gain("downwards dynamic range gain scan");
            adaptive_range_state = RANGE_SCAN_DOWN;
            adaptive_range_delay = Modes.adaptive_range_scan_delay;
            break;
        }

        if (sdrGetGain() >= adaptive_gain_max) {
            // We have reached our upper gain limit
            fprintf(stderr, "adaptive: reached upper gain limit, halting dynamic range scan here\n");
            adaptive_range_state = RANGE_SCAN_IDLE;
            adaptive_range_delay = Modes.adaptive_range_rescan_delay;
            break;
        }

        // This gain step is OK and we have more to try, try the next gain step up.
        fprintf(stderr, "adaptive: available dynamic range (%.1fdB) >= required dynamic range (%.1fdB), continuing upward scan\n", available_range, Modes.adaptive_range_target);
        adaptive_increase_gain("upwards dynamic range scan");
        adaptive_range_delay = Modes.adaptive_range_scan_delay;
        break;

    case RANGE_SCAN_DOWN:
        if (adaptive_range_delay > 0)
            break;
        
        if (available_range >= Modes.adaptive_range_target) {
            // Current gain meets our target; we are done with the scan.
            fprintf(stderr, "adaptive: available dynamic range (%.1fdB) >= required dynamic range (%.1fdB), stopping downwards scan here\n", available_range, Modes.adaptive_range_target);
            adaptive_range_state = RANGE_SCAN_IDLE;
            adaptive_range_delay = Modes.adaptive_range_rescan_delay;
            break;
        }

        if (sdrGetGain() <= adaptive_gain_min) {
            fprintf(stderr, "adaptive: reached lower gain limit, halting dynamic range scan here\n");
            adaptive_range_state = RANGE_SCAN_IDLE;
            adaptive_range_delay = Modes.adaptive_range_rescan_delay;
            break;
        }

        // This gain step is too loud and we have more to try, try the next gain step down
        fprintf(stderr, "adaptive: available dynamic range (%.1fdB) < required dynamic range (%.1fdB), continuing downwards scan\n", available_range, Modes.adaptive_range_target);
        adaptive_decrease_gain("downwards dynamic range gain scan");
        adaptive_range_delay = Modes.adaptive_range_scan_delay;
        break;

    case RANGE_SCAN_IDLE:
        // Look for increased noise that could be compensated for by decreasing gain.
        // Do this even if we're delaying.
        if (available_range + adaptive_gain_down_db / 2 < Modes.adaptive_range_target && sdrGetGain() > adaptive_gain_min) {
            fprintf(stderr, "adaptive: available dynamic range (%.1fdB) + half gain step down (%.1fdB) < required dynamic range (%.1fdB), starting downward scan\n",
                    available_range, Modes.adaptive_range_target, adaptive_gain_down_db);
            adaptive_range_state = RANGE_SCAN_DOWN;
            adaptive_range_delay = Modes.adaptive_range_scan_delay;
            break;
        }

        if (adaptive_range_delay > 0)
            break;
        
        // Infrequently consider increasing gain to handle the case where we've selected a too-low gain where the noise floor is dominated by noise unrelated to the gain setting
        if (available_range >= Modes.adaptive_range_target && sdrGetGain() < adaptive_gain_max) {
            fprintf(stderr, "adaptive: start periodic scan for acceptable dynamic range at increased gain\n");
            adaptive_increase_gain("upwards dynamic range scan");
            adaptive_range_state = RANGE_SCAN_UP;
            adaptive_range_delay = Modes.adaptive_range_scan_delay;
            break;
        }

        // Nothing to do for a while.
        adaptive_range_delay = Modes.adaptive_range_rescan_delay;
        break;

    default:
        fprintf(stderr, "adaptive: in a weird state (%d), trying to fix it\n", adaptive_range_state);
        adaptive_range_state = RANGE_SCAN_IDLE;
        adaptive_range_delay = Modes.adaptive_range_scan_delay;
        break;
    }
}
