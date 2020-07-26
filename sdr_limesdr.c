// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// sdr_limesdr.c: LimeSDR dongle support
//
// Copyright (c) 2020 Gluttton <gluttton@ukr.net>
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

// This file incorporates work covered by the following copyright and
// permission notice:
//
//   Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are
//   met:
//
//    *  Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//    *  Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dump1090.h"
#include "sdr_limesdr.h"

#include <lime/LimeSuite.h>

static struct {
    lms_device_t *dev;
    lms_stream_t stream;
    bool is_stream_opened;
    bool is_stop;
    int bytes_in_sample;
    iq_convert_fn converter;
    struct converter_state *converter_state;
} LimeSDR;

void limesdrLogHandler(int lvl, const char *msg)
{
    FILE *out = NULL;
    switch (lvl) {
        default:
        case LMS_LOG_DEBUG:
            return;
        case LMS_LOG_INFO:
            out = stdout;
            break;
        case LMS_LOG_WARNING:
        case LMS_LOG_ERROR:
        case LMS_LOG_CRITICAL:
            out = stderr;
            break;
    }

    fprintf(out, "limesdr: %s\n", msg);
}

void limesdrInitConfig()
{
    LimeSDR.dev = NULL;
    LimeSDR.stream.channel = 0;
    LimeSDR.stream.fifoSize = 1024 * 1024;
    LimeSDR.stream.throughputVsLatency = 1.0; // best throughput
    LimeSDR.stream.isTx = false;
    LimeSDR.stream.dataFmt = LMS_FMT_I16; // should be matched with conveter
    LimeSDR.is_stream_opened = false;
    LimeSDR.is_stop = false;
    LimeSDR.bytes_in_sample = 2 * sizeof(int16_t); // hardcoded for LMS_FMT_I16

    LMS_RegisterLogHandler(limesdrLogHandler);
}

void limesdrShowHelp()
{
    printf("      limesdr-specific options (use with --device-type limesdr)\n");
    printf("\n");
    printf("so far there is no any LimeSDR specific option...\n");
    printf("\n");
}

bool limesdrHandleOption(int argc, char **argv, int *jptr)
{
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);
    MODES_NOTUSED(jptr);

    return false;
}

bool limesdrOpen(void)
{
    const size_t devCountMax = 8;
    lms_info_str_t list[devCountMax];
    const int devCount = LMS_GetDeviceList(list);
    if (devCount < 0) {
        fprintf(stderr, "limesdr: unable to get a number of connected devices\n");
        goto error;
    }

    if (devCount < 1) {
        fprintf(stderr, "limesdr: no connected devices\n");
        goto error;
    }

    if (LMS_Open(&LimeSDR.dev, list[0], NULL) ) {
        fprintf(stderr, "limesdr: unable to open device\n");
        goto error;
    }

    if (LMS_Init(LimeSDR.dev)) {
        fprintf(stderr, "limesdr: unable to initialize device\n");
        goto error;
    }

    if (LMS_EnableChannel(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, true)) {
        fprintf(stderr, "limesdr: unable to enable RX channel\n");
        goto error;
    }

    if (LMS_SetLOFrequency(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, Modes.freq)) {
        fprintf(stderr, "limesdr: unable to set frequency\n");
        goto error;
    }

    if (LMS_SetAntenna(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, LMS_PATH_LNAL)) {
        fprintf(stderr, "limesdr: unable to set RF port\n");
        goto error;
    }

    if (LMS_SetSampleRate(LimeSDR.dev, Modes.sample_rate, 0/*default oversample*/)) {
        fprintf(stderr, "limesdr: unable to set sampling rate\n");
        goto error;
    }

    if (LMS_SetNormalizedGain(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, 0.85)) {
        fprintf(stderr, "limesdr: unable to set gain\n");
        goto error;
    }

    if (LMS_SetLPFBW(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, Modes.sample_rate)) {
        fprintf(stderr, "limesdr: unable to set LP filter\n");
        goto error;
    }

    LimeSDR.is_stream_opened = true;
    if (LMS_SetupStream(LimeSDR.dev, &LimeSDR.stream)) {
        fprintf(stderr, "limesdr: unable to setup stream\n");
        LimeSDR.is_stream_opened = false;
        goto error;
    }

    if (LMS_Calibrate(LimeSDR.dev, LMS_CH_RX, LimeSDR.stream.channel, 2.5e6/*0.5e5*/, 0)) {
        fprintf(stderr, "limesdr: unable to calibrate device\n");
        goto error;
    }

    LimeSDR.converter = init_converter(INPUT_SC16,
                                      Modes.sample_rate,
                                      Modes.dc_filter,
                                      &LimeSDR.converter_state);
    if (!LimeSDR.converter) {
        fprintf(stderr, "limesdr: can't initialize sample converter.\n");
        goto error;
    }

    return true;

  error:
    if (LimeSDR.is_stream_opened) {
        LMS_DestroyStream(LimeSDR.dev, &LimeSDR.stream);
        LimeSDR.is_stream_opened = false;
    }

    if (LimeSDR.dev) {
        LMS_Close(LimeSDR.dev);
        LimeSDR.dev = NULL;
    }

    return false;
}

static struct timespec limesdr_thread_cpu;

void limesdrCallback(unsigned char *buf, uint32_t len, void *ctx)
{
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    unsigned block_duration;

    static int dropping = 0;
    static uint64_t sampleCounter = 0;

    MODES_NOTUSED(ctx);

    // Lock the data buffer variables before accessing them
    pthread_mutex_lock(&Modes.data_mutex);
    if (Modes.exit) {
        LimeSDR.is_stop = true; // ask our caller to exit
    }

    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;

    // Paranoia! Unlikely, but let's go for belt and suspenders here

    if (len != MODES_RTL_BUF_SIZE) {
        fprintf(stderr, "weirdness: limesdr gave us a block with an unusual size (got %u bytes, expected %u bytes)\n",
                (unsigned)len, (unsigned)MODES_RTL_BUF_SIZE);

        if (len > MODES_RTL_BUF_SIZE) {
            // wat?! Discard the start.
            unsigned discard = (len - MODES_RTL_BUF_SIZE + 1) / LimeSDR.bytes_in_sample;
            outbuf->dropped += discard;
            buf += discard * LimeSDR.bytes_in_sample;
            len -= discard * LimeSDR.bytes_in_sample;
        }
    }

    slen = len / LimeSDR.bytes_in_sample; // Drops any trailing odd sample, that's OK

    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS/2)) {
        // FIFO is full. Drop this block.
        dropping = 1;
        outbuf->dropped += slen;
        sampleCounter += slen;
        pthread_mutex_unlock(&Modes.data_mutex);
        return;
    }

    dropping = 0;
    pthread_mutex_unlock(&Modes.data_mutex);

    // Compute the sample timestamp and system timestamp for the start of the block
    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += slen;

    // Get the approx system time for the start of this block
    block_duration = 1e3 * slen / Modes.sample_rate;
    outbuf->sysTimestamp = mstime() - block_duration;

    // Copy trailing data from last block (or reset if not valid)
    if (outbuf->dropped == 0) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof(uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof(uint16_t));
    }

    // Convert the new data
    outbuf->length = slen;
    LimeSDR.converter(buf, &outbuf->data[Modes.trailing_samples], slen, LimeSDR.converter_state, &outbuf->mean_level, &outbuf->mean_power);

    // Push the new data to the demodulation thread
    pthread_mutex_lock(&Modes.data_mutex);

    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0;  // just in case
    Modes.first_free_buffer = next_free_buffer;

    // accumulate CPU while holding the mutex, and restart measurement
    end_cpu_timing(&limesdr_thread_cpu, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&limesdr_thread_cpu);

    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);
}

void limesdrRun()
{
    if (!LimeSDR.dev) {
        return;
    }

    int16_t *buffer = malloc(MODES_RTL_BUF_SIZE);

    LMS_StartStream(&LimeSDR.stream);

    start_cpu_timing(&limesdr_thread_cpu);

    while (!LimeSDR.is_stop) {
        int sampleCnt = LMS_RecvStream(&LimeSDR.stream, buffer, MODES_RTL_BUF_SIZE / LimeSDR.bytes_in_sample, NULL, 1000);
        if (sampleCnt) {
            limesdrCallback((unsigned char *)buffer, sampleCnt * LimeSDR.bytes_in_sample, NULL);
        }
    }

    if (!Modes.exit) {
        fprintf(stderr, "limesdr: async read returned unexpectedly.\n");
    }

    free(buffer);
    LMS_StopStream(&LimeSDR.stream);
}

void limesdrClose()
{
    if (LimeSDR.converter) {
        cleanup_converter(LimeSDR.converter_state);
        LimeSDR.converter = NULL;
        LimeSDR.converter_state = NULL;
    }

    LMS_StopStream(&LimeSDR.stream);

    if (LimeSDR.is_stream_opened) {
        LMS_DestroyStream(LimeSDR.dev, &LimeSDR.stream);
        LimeSDR.is_stream_opened = false;
    }

    if (LimeSDR.dev) {
        LMS_Close(LimeSDR.dev);
        LimeSDR.dev = NULL;
    }
}
