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

    iq_convert_fn converter;
    struct converter_state *converter_state;
} SOAPY;

//
// =============================== SoapySDR handling ==========================
//

void soapyInitConfig()
{
    SOAPY.dev = NULL;
    SOAPY.stream = NULL;
    SOAPY.converter = NULL;
    SOAPY.converter_state = NULL;
}

void soapyShowHelp()
{
    printf("      SoapySDR-specific options (use with --device-type soapy)\n");
    printf("\n");
    printf("--device <string>        select/configure device\n");
    printf("\n");
}

bool soapyHandleOption(int argc, char **argv, int *jptr)
{
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);
    MODES_NOTUSED(jptr);
    return false;
}

bool soapyOpen(void)
{
    size_t length;
    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    for (size_t i = 0; i < length; i++)
    {
        printf("Found device #%d: ", (int)i);
        for (size_t j = 0; j < results[i].size; j++)
        {
            printf("%s=%s, ", results[i].keys[j], results[i].vals[j]);
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
    for (size_t i = 0; i < result.size; ++i) {
        printf("%s=%s\n", result.keys[i], result.vals[i]);
    }
    SoapySDRKwargs_clear(&result);

    if (SoapySDRDevice_setSampleRate(SOAPY.dev, SOAPY_SDR_RX, 0, Modes.sample_rate) != 0) {
        fprintf(stderr, "soapy: setSampleRate failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }
    
    if (SoapySDRDevice_setFrequency(SOAPY.dev, SOAPY_SDR_RX, 0, Modes.freq, NULL) != 0) {
        fprintf(stderr, "soapy: setFrequency failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    if (SoapySDRDevice_setGain(SOAPY.dev, SOAPY_SDR_RX, 0, Modes.gain / 10.0) != 0) {
        fprintf(stderr, "soapy: setGain failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    if (SoapySDRDevice_setBandwidth(SOAPY.dev, SOAPY_SDR_RX, 0, 3.0e6) != 0) {
        fprintf(stderr, "soapy: setBandwidth failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    if (SoapySDRDevice_setAntenna(SOAPY.dev, SOAPY_SDR_RX, 0, "LNAW") != 0) {
        fprintf(stderr, "soapy: setAntenna failed: %s\n", SoapySDRDevice_lastError());
        goto error;
    }

    fprintf(stderr, "soapy: frequency is %.1f MHz\n",
            SoapySDRDevice_getFrequency(SOAPY.dev, SOAPY_SDR_RX, 0) / 1e6);
    fprintf(stderr, "soapy: sample rate is %.1f MHz\n",
            SoapySDRDevice_getSampleRate(SOAPY.dev, SOAPY_SDR_RX, 0) / 1e6);
    fprintf(stderr, "soapy: bandwidth is %.1f MHz\n",
            SoapySDRDevice_getBandwidth(SOAPY.dev, SOAPY_SDR_RX, 0) / 1e6);
    fprintf(stderr, "soapy: gain mode is %d\n",
            SoapySDRDevice_getGainMode(SOAPY.dev, SOAPY_SDR_RX, 0));
    fprintf(stderr, "soapy: gain is %.1f dB\n",
            SoapySDRDevice_getGain(SOAPY.dev, SOAPY_SDR_RX, 0));
    fprintf(stderr, "soapy: antenna is %s\n",
            SoapySDRDevice_getAntenna(SOAPY.dev, SOAPY_SDR_RX, 0));
    
    size_t channels[1] = { 0 };
    if (SoapySDRDevice_setupStream(SOAPY.dev, &SOAPY.stream, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, NULL) != 0) {
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

    void *buffers[1];
    const int buffer_elements = 131072;
    buffers[0] = malloc(buffer_elements * 4);

    while (!Modes.exit) {
        int flags;
        long long timeNs;
        int sample_count = SoapySDRDevice_readStream(SOAPY.dev, SOAPY.stream, buffers, buffer_elements, &flags, &timeNs, 5000000);
        if (sample_count <= 0) {
            fprintf(stderr, "soapy: readStream failed: %s\n", SoapySDRDevice_lastError());
            return;
        }
        
        pthread_mutex_lock(&Modes.data_mutex);
        unsigned next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
        if (next_free_buffer == Modes.first_filled_buffer) {
            // drop it
            fprintf(stderr, "fifo full\n");            
            pthread_mutex_unlock(&Modes.data_mutex);
            continue;
        }

        struct mag_buf *outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
        struct mag_buf *lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
        pthread_mutex_unlock(&Modes.data_mutex);

        // Copy trailing data from last block
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof(uint16_t));
        SOAPY.converter(buffers[0], &outbuf->data[Modes.trailing_samples], sample_count, 
                        SOAPY.converter_state, &outbuf->mean_level, &outbuf->mean_power);
        outbuf->dropped = 0;
        outbuf->length = sample_count;

        pthread_mutex_lock(&Modes.data_mutex);
        Modes.mag_buffers[next_free_buffer].dropped = 0;
        Modes.mag_buffers[next_free_buffer].length = 0;
        Modes.first_free_buffer = next_free_buffer;
        pthread_cond_signal(&Modes.data_cond);
        pthread_mutex_unlock(&Modes.data_mutex);
    }
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

    fprintf(stderr, "all done\n");
}
