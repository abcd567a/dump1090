#include "dump1090.h"
#include "autogain.h"

//
// block handling
//

static unsigned autogain_block_remaining;
static unsigned autogain_block_size;

void autogain_init();
void autogain_update(uint16_t *buf, unsigned length, struct modesMessage *decoded);
static void autogain_update_single(uint16_t *buf, unsigned length, struct modesMessage *decoded);
static void autogain_end_of_block();

//
// burst handling
//

static unsigned autogain_burst_window_size;
static unsigned autogain_burst_window_remaining;
static unsigned autogain_burst_window_counter;
static unsigned autogain_burst_runlength;
static unsigned autogain_burst_block_counter;
static double autogain_burst_smoothed;

static void autogain_burst_update(uint16_t *buf, unsigned length);
static void autogain_burst_skip(unsigned length);
static unsigned autogain_burst_scan_partial(uint16_t *buf, unsigned n);
static void autogain_burst_scan(uint16_t *buf, unsigned windows);
static void autogain_burst_end_of_window(unsigned counter);

//
// noise handling
//

static unsigned *autogain_noise_radix;
static unsigned autogain_noise_counter;
static double autogain_noise_smoothed;

static void autogain_noise_update(uint16_t *buf, unsigned length);

void autogain_init()
{
    //if (!Modes.autogain_enabled)
    //    return;

    // Look for 40us bursts
    autogain_burst_window_size = Modes.sample_rate / 25000;
    autogain_burst_window_remaining = autogain_burst_window_size;
    autogain_burst_window_counter = 0;

    // Use an overall block size that is an exact multiple of the burst window, close to 1 second long
    autogain_block_size = autogain_burst_window_size * 25000;
    autogain_block_remaining = autogain_block_size;

    autogain_noise_radix = calloc(sizeof(unsigned), 65536);

}

// Feed some samples into the autogain system. Any number of samples might be passed in.
void autogain_update(uint16_t *buf, unsigned length, struct modesMessage *decoded)
{
    //if (!Modes.autogain_enabled)
    //    return;

    //fprintf(stderr, "%p+%u %p\n", buf, length, decoded);

    // process samples up to a block boundary, then process the completed block
    while (length >= autogain_block_remaining) {
        autogain_update_single(buf, autogain_block_remaining, decoded);
        buf += autogain_block_remaining;
        length -= autogain_block_remaining;

        autogain_end_of_block();
        autogain_block_remaining = autogain_block_size;
    }

    // process final samples that don't complete a block
    if (length > 0) {
        autogain_update_single(buf, length, decoded);
        autogain_block_remaining -= length;
    }
}

// Feed some samples into the autogain system. The samples are guaranteed to not cross a block boundary.
static void autogain_update_single(uint16_t *buf, unsigned length, struct modesMessage *decoded)
{
    if (decoded) {
        autogain_burst_skip(length);
    } else {
        autogain_burst_update(buf, length);
        autogain_noise_update(buf, length);
    }
}

static void autogain_burst_skip(unsigned length)
{
    // first window
    if (length < autogain_burst_window_remaining) {
        // partial fill
        autogain_burst_window_remaining -= length;
        return;
    }

    // skip remainder of first window, dispatch it
    autogain_burst_end_of_window(autogain_burst_window_counter);
    length -= autogain_burst_window_remaining;

    // skip remaining windows, dispatch them
    unsigned windows = length / autogain_burst_window_size;
    unsigned samples = windows * autogain_burst_window_size;
    while (--windows)
        autogain_burst_end_of_window(0);

    length -= samples;

    // final partial window
    autogain_burst_window_counter = 0;
    autogain_burst_window_remaining = autogain_burst_window_size - length;
}

static void autogain_burst_update(uint16_t *buf, unsigned length)
{
    // first window
    if (length < autogain_burst_window_remaining) {
        // partial fill
        autogain_burst_window_counter += autogain_burst_scan_partial(buf, length);
        autogain_burst_window_remaining -= length;
        return;
    }

    // complete fill of first partial window
    unsigned n = autogain_burst_window_remaining;
    unsigned counter = autogain_burst_window_counter + autogain_burst_scan_partial(buf, n);
    autogain_burst_end_of_window(counter);
    buf += n;
    length -= n;

    // remaining windows
    unsigned windows = length / autogain_burst_window_size;
    unsigned samples = windows * autogain_burst_window_size;
    autogain_burst_scan(buf, windows);
    buf += samples;
    length -= samples;

    // final partial window
    autogain_burst_window_counter = autogain_burst_scan_partial(buf, length);
    autogain_burst_window_remaining = autogain_burst_window_size - length;
}

static inline unsigned autogain_burst_scan_partial(uint16_t *buf, unsigned n)
{
    unsigned counter = 0;
    while (n--) {
        if (buf[0] > (65536 * 3 / 4))
            ++counter;
        ++buf;
    }
    return counter;
}

static void autogain_burst_scan(uint16_t *buf, unsigned windows)
{
    while (windows--) {
        unsigned counter = autogain_burst_scan_partial(buf, autogain_burst_window_size);
        buf += autogain_burst_window_size;
        autogain_burst_end_of_window(counter);
    }
}

static void autogain_burst_end_of_window(unsigned counter)
{
    if (counter > autogain_burst_window_size / 4) {
        ++autogain_burst_runlength;
    } else {
        // Look for runs of >= 80us
        if (autogain_burst_runlength >= 2)
            ++autogain_burst_block_counter;
        autogain_burst_runlength = 0;
    }
}

static void autogain_noise_update(uint16_t *buf, unsigned length)
{
    autogain_noise_counter += length;
    while (length--) {
        ++autogain_noise_radix[buf[0]];
        ++buf;
    }
}

static void autogain_noise_end_of_block()
{
    unsigned n = 0, i = 0;

    unsigned count_40 = autogain_noise_counter * 40 / 100;
    while (i < 65536 && n <= count_40)
        n += autogain_noise_radix[i++];
    uint16_t percentile_40 = i - 1;

    autogain_noise_smoothed = autogain_noise_smoothed * 0.9 + percentile_40 * 0.1;
    if (autogain_noise_smoothed > 0) {
        Modes.stats_current.autogain_noise_dbfs = 20 * log10(autogain_noise_smoothed / 65536.0);
    } else {
        Modes.stats_current.autogain_noise_dbfs = 0;
    }

    memset(autogain_noise_radix, 0, 65536 * sizeof(unsigned));
    autogain_noise_counter = 0;
}

static void autogain_burst_end_of_block()
{
    autogain_burst_smoothed = autogain_burst_smoothed * 0.8 + autogain_burst_block_counter * 0.2;
    Modes.stats_current.autogain_bursts_per_second = autogain_burst_smoothed;

    autogain_burst_block_counter = 0;
}

// Called once at each block boundary.
static void autogain_end_of_block()
{
    autogain_noise_end_of_block();
    autogain_burst_end_of_block();
}
