// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// sdr_soapy.c: SoapySDR support
//
// Copyright (c) 2014-2017 Oliver Jowett <oliver@mutability.co.uk>
// Copyright (c) 2017 FlightAware LLC
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
#include "sdr_soapy.h"

#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

static struct {
    SoapySDRDevice *dev;
    SoapySDRStream *stream;
    char* hw_key;
    bool dev_sdrplay;

    iq_convert_fn converter;
    struct converter_state *converter_state;

    size_t channel;
    const char* antenna;
    bool enable_agc;
} SOAPY;

//
// =============================== SoapySDR handling ==========================
//

void soapyInitConfig()
{
    SOAPY.dev = NULL;
    SOAPY.stream = NULL;
    SOAPY.hw_key = NULL;
    SOAPY.dev_sdrplay = false;
    SOAPY.converter = NULL;
    SOAPY.converter_state = NULL;
    SOAPY.channel = 0;
    SOAPY.antenna = NULL;
    SOAPY.enable_agc = false;
}

void soapyShowHelp()
{
    printf("      SoapySDR-specific options (use with --device-type soapy)\n");
    printf("\n");
    printf("--device <string>        select/configure device\n");
    printf("--channel <num>          select channel if device supports multiple channels (default: 0)\n");
    printf("--antenna <string>       select antenna (default depends on device)\n");
    printf("--enable-agc             enable Automatic Gain Control if supported by device\n");

    printf("\n");
}

bool soapyHandleOption(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    bool more = (j +1  < argc);

    if (!strcmp(argv[j], "--channel")) {
        SOAPY.channel = atoi(argv[++j]);
    } else if (!strcmp(argv[j], "--antenna") && more) {
        SOAPY.antenna = strdup(argv[++j]);
    } else if (!strcmp(argv[j], "--enable-agc") && more) {
        SOAPY.enable_agc = true;
    } else {
        return false;
    }

    *jptr = j;
    return true;
}

bool soapyOpen(void)
{
    size_t length = 0;
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    for (size_t i = 0; i < length; i++)
    {
        printf("Found device #%d: ", (int)i);
        for (size_t j = 0; j < results[i].size; j++)
        {
            printf("%s=%s", results[i].keys[j], results[i].vals[j]);
            if (j+1 < results[i].size) printf(", ");
            if (!strncmp(results[i].keys[j], "driver", 6)) {
                if (!strncmp(results[i].vals[j], "sdrplay", 7)) {
                    SOAPY.dev_sdrplay = 1;
                }
            }
        }
        printf("\n");
    }
    SoapySDRKwargsList_clear(results, length);

    SOAPY.dev = SoapySDRDevice_makeStrArgs(Modes.dev_name);
    if (!SOAPY.dev) {
        fprintf(stderr, "soapy: failed to create device: %s\n", SoapySDRDevice_lastError());
        return false;
    }

    SoapySDRKwargs result = SoapySDRDevice_getHardwareInfo(SOAPY.dev);
    if (result.size > 0) {
        for (size_t i = 0; i < result.size; ++i) {
            printf("%s=%s", result.keys[i], result.vals[i]);
            if (i+1 < result.size) printf(", ");
        }
        printf("\n");
    }
    SoapySDRKwargs_clear(&result);

    char* hw_key = SoapySDRDevice_getHardwareKey(SOAPY.dev);
    if (hw_key) {
        printf("soapy: hardware key is %s\n", hw_key);
        SOAPY.hw_key = strdup(hw_key);
        SoapySDR_free(hw_key);
    }

    if (SOAPY.channel) {
        size_t supported_channels = SoapySDRDevice_getNumChannels(SOAPY.dev, SOAPY_SDR_RX);
        if (SOAPY.channel >= supported_channels) {
            fprintf(stderr, "soapy: device only supports %ld channels\n", supported_channels);
            goto error;
        }
    }

    length = 0;
    char** available_antennas = SoapySDRDevice_listAntennas(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, &length);
    fprintf(stderr, "soapy: available antennas: ");
    for (size_t i = 0; i < length; i++) {
        fprintf(stderr, "%s", available_antennas[i]);
        if (i+1 < length) printf(", ");
    }
    fprintf(stderr, "\n");
    SoapySDRStrings_clear(&available_antennas, length);

    if (SoapySDRDevice_setSampleRate(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, Modes.sample_rate) != 0) {
        fprintf(stderr, "soapy: setSampleRate failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    if (SoapySDRDevice_setFrequency(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, Modes.freq, NULL) != 0) {
        fprintf(stderr, "soapy: setFrequency failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    bool has_agc = SoapySDRDevice_hasGainMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel);
    if (SOAPY.enable_agc) {
        if (has_agc) {
            if (SoapySDRDevice_setGainMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, 1) != 0) {
                fprintf(stderr, "soapy: setGainMode failed: %s\n", SoapySDRDevice_lastError());
                goto error;
            }
        }
        else {
            fprintf(stderr, "soapy: device does not support enabling AGC\n");
            goto error;
        }
    }
    else if(has_agc) {
        // by default, disable AGC
        if (SoapySDRDevice_setGainMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, 0) != 0) {
            fprintf(stderr, "soapy: setGainMode failed: %s\n", SoapySDRDevice_lastError());
            goto error;
        }
    }

    if (Modes.gain != MODES_DEFAULT_GAIN) {
        double gain;
        if (SOAPY.dev_sdrplay) {
            gain = Modes.gain;
        } else {
            gain = Modes.gain / 10.0;
        }
        if (SoapySDRDevice_setGain(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, gain) != 0) {
            fprintf(stderr, "soapy: setGain failed: %s\n", SoapySDRDevice_lastError());
            goto error;
        }
    }

    if (SoapySDRDevice_setBandwidth(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, (SOAPY.dev_sdrplay ? 5.0e6 : 3.0e6)) != 0) {
        fprintf(stderr, "soapy: setBandwidth failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    if (SOAPY.antenna && SoapySDRDevice_setAntenna(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, SOAPY.antenna) != 0) {
        fprintf(stderr, "soapy: setAntenna failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    fprintf(stderr, "soapy: frequency is %.1f MHz\n",
            SoapySDRDevice_getFrequency(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) / 1e6);
    fprintf(stderr, "soapy: sample rate is %.1f MHz\n",
            SoapySDRDevice_getSampleRate(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) / 1e6);
    fprintf(stderr, "soapy: bandwidth is %.1f MHz\n",
            SoapySDRDevice_getBandwidth(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) / 1e6);
    if (has_agc) {
        fprintf(stderr, "soapy: AGC mode is %s\n",
                SoapySDRDevice_getGainMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) == 0 ? "disabled" : "enabled");
    }
    fprintf(stderr, "soapy: gain is %.1f dB\n",
            SoapySDRDevice_getGain(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel));
    char* antenna = SoapySDRDevice_getAntenna(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel);
    if (antenna != NULL) {
        fprintf(stderr, "soapy: antenna is %s\n", antenna);
        SoapySDR_free(antenna);
    }
    if (SoapySDRDevice_hasDCOffsetMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel)) {
        fprintf(stderr, "soapy: DC offset mode is %s\n",
                SoapySDRDevice_getDCOffsetMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) ? "enabled" : "disabled");
    }
    if (SoapySDRDevice_hasDCOffset(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel)) {
        double offsetI;
        double offsetQ;
        if (!SoapySDRDevice_getDCOffset(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, &offsetI, &offsetQ)) {
            fprintf(stderr, "soapy: DC offset is I=%.1f, Q=%.1f\n", offsetI, offsetQ);
        }
    }
    if (SoapySDRDevice_hasIQBalanceMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel)) {
        fprintf(stderr, "soapy: IQ balance mode is %s\n",
                SoapySDRDevice_getIQBalanceMode(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel) ? "enabled" : "disabled");
    }
    if (SoapySDRDevice_hasIQBalance(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel)) {
          double balanceI;
          double balanceQ;
          if (!SoapySDRDevice_getIQBalance(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel, &balanceI, &balanceQ)) {
              fprintf(stderr, "soapy: IQ balance is I=%.1f, Q=%.1f\n", balanceI, balanceQ);
          }
    }
    if (SoapySDRDevice_hasFrequencyCorrection(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel)) {
        fprintf(stderr, "soapy: frequency correction is %.1f ppm\n",
                SoapySDRDevice_getFrequencyCorrection(SOAPY.dev, SOAPY_SDR_RX, SOAPY.channel));
    }

    size_t channels[1] = { 0 };
    SoapySDRKwargs stream_args = { 0 };
    if ((SOAPY.stream = SoapySDRDevice_setupStream(SOAPY.dev, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, &stream_args)) == NULL) {
        fprintf(stderr, "soapy: setupStream failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    SOAPY.converter = init_converter(INPUT_SC16,
                                     Modes.sample_rate,
                                     Modes.dc_filter,
                                     &SOAPY.converter_state);
    if (!SOAPY.converter) {
        fprintf(stderr, "soapy: can't initialize sample converter\n");
        goto error;
    }

    return true;

 error:
    if (SOAPY.dev != NULL) {
        SoapySDRDevice_unmake(SOAPY.dev);
        SOAPY.dev = NULL;
    }

    return false;
}

void soapyRun()
{
    if (!SOAPY.dev) {
        return;
    }

    if (SoapySDRDevice_activateStream(SOAPY.dev, SOAPY.stream, 0, 0, 0) != 0) {
        fprintf(stderr, "soapy: activateStream failed: %s\n", SoapySDRDevice_lastError());
        return;
    }

    uint8_t* buf;
    const int buffer_elements = MODES_MAG_BUF_SAMPLES; // 131072
    buf = malloc(buffer_elements * 4);

    unsigned int dropped = 0;
    uint64_t sampleCounter = 0;

    while (!Modes.exit) {
        int flags;
        long long timeNs;
        int samples_read = SoapySDRDevice_readStream(SOAPY.dev, SOAPY.stream, (void *) &buf, buffer_elements, &flags, &timeNs, 5000000);
        if (samples_read <= 0) {
            fprintf(stderr, "soapy: readStream failed: %s\n", SoapySDRDevice_lastError());
            return;
        }

        struct mag_buf *outbuf = fifo_acquire(0 /* no wait */);
        if (!outbuf) {
            fprintf(stderr, "soapy: fifo is full, dropping samples\n");
            // FIFO is full. Drop this block.
            dropped += samples_read;
            sampleCounter += samples_read;
            continue;
        }

        outbuf->flags = 0;

        if (dropped) {
            // We previously dropped some samples due to no buffers being available
            outbuf->flags |= MAGBUF_DISCONTINUOUS;
            outbuf->dropped = dropped;
        }

        dropped = 0;

        // Compute the sample timestamp and system timestamp for the start of the block
        outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
        sampleCounter += samples_read;

        // Get the approx system time for the start of this block
        unsigned block_duration = 1e3 * samples_read / Modes.sample_rate;
        outbuf->sysTimestamp = mstime() - block_duration;

        unsigned int to_convert = samples_read;
        if (to_convert + outbuf->overlap > outbuf->totalLength) {
            // how did that happen?
            to_convert = outbuf->totalLength - outbuf->overlap;
            dropped = samples_read - to_convert;
        }

        // Convert the new data
        SOAPY.converter(buf, &outbuf->data[outbuf->overlap], to_convert, SOAPY.converter_state, &outbuf->mean_level, &outbuf->mean_power);
        outbuf->validLength = outbuf->overlap + to_convert;

        // Push to the demodulation thread
        fifo_enqueue(outbuf);
    }

    free(buf);
}

void soapyClose()
{
    fprintf(stderr, "close stream\n");
    if (SOAPY.stream) {
        SoapySDRDevice_closeStream(SOAPY.dev, SOAPY.stream);
        SOAPY.stream = NULL;
    }

    fprintf(stderr, "close device\n");
    if (SOAPY.dev) {
        SoapySDRDevice_unmake(SOAPY.dev);
        SOAPY.dev = NULL;
    }

    if (SOAPY.converter) {
        cleanup_converter(SOAPY.converter_state);
        SOAPY.converter = NULL;
        SOAPY.converter_state = NULL;
    }

    if (SOAPY.hw_key) {
        free(SOAPY.hw_key);
        SOAPY.hw_key = NULL;
    }

    fprintf(stderr, "all done\n");
}
