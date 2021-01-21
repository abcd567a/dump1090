// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// sdr_pxsdr: pxsdr support
//
// Copyright (c) 2020-2021 FlightAware LLC
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
#include "sdr_pxsdr.h"

#include "pxsdr.h"

#define PXSDR_BUFFERS 4
#define PXSDR_BUFSIZE MODES_MAG_BUF_SAMPLES

static struct {
    pxsdr_context *context;
    pxsdr_device_handle *dev;

    // path to firmware image
    char *firmware_path;
    // decimation factor
    unsigned decimation;

    int vga_gain;
    int lna_gain;
    int mixer_gain;

    int bandwidth;

    iq_convert_fn converter;
    struct converter_state *converter_state;

    uint64_t expected_timestamp;
} PXSDR;

static void pxsdrCleanup();

static bool handle_one_buffer(pxsdr_sample_block *block, void *user_data);

static void pxsdr_logger(pxsdr_context *ctx, pxsdr_log_level level, const char *message)
{
    MODES_NOTUSED(ctx);
    MODES_NOTUSED(level);

    if (level == PXSDR_LOG_DEBUG)
        fprintf(stderr, "PXSDR: debug: %s\n", message);
    else
        fprintf(stderr, "PXSDR: %s\n", message);
}

void pxsdrInitConfig()
{
    PXSDR.context = NULL;
    PXSDR.dev = NULL;
    PXSDR.firmware_path = NULL;
    PXSDR.decimation = 4;
    PXSDR.converter = NULL;
    PXSDR.converter_state = NULL;
    PXSDR.lna_gain = PXSDR.vga_gain = PXSDR.mixer_gain = -1;
    PXSDR.bandwidth = 2400000;
}

bool pxsdrHandleOption(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    bool more = (j +1  < argc);

    if (!strcmp(argv[j], "--pxsdr-firmware") && more) {
        free(PXSDR.firmware_path);
        PXSDR.firmware_path = strdup(argv[++j]);
    } else if (!strcmp(argv[j], "--pxsdr-decimation") && more) {
        PXSDR.decimation = atoi(argv[++j]);
    } else if (!strcmp(argv[j], "--pxsdr-vga-gain") && more) {
        PXSDR.vga_gain = atoi(argv[++j]);
    } else if (!strcmp(argv[j], "--pxsdr-lna-gain") && more) {
        PXSDR.lna_gain = atoi(argv[++j]);
    } else if (!strcmp(argv[j], "--pxsdr-mixer-gain") && more) {
        PXSDR.mixer_gain = atoi(argv[++j]);
    } else if (!strcmp(argv[j], "--pxsdr-bandwidth") && more) {
        PXSDR.bandwidth = atoi(argv[++j]);
    } else {
        return false;
    }

    *jptr = j;
    return true;
}

void pxsdrShowHelp()
{
    printf("      pxsdr-specific options (use with --sdr-type pxsdr)\n");
    printf("\n");
    printf("--device <serial>        use PXSDR dongle with this serial number\n");
    printf("--pxsdr-firmware <path>  set path to PXSDR firmware image\n");
    printf("--pxsdr-index <index>    use PXSDR dongle with this index (1 = first detected device, etc)\n");
    printf("--pxsdr-decimation <n>   set decimation factor (must be a power of 2); default 1\n");
    printf("\n");
}

static void pxsdrEnumerate()
{
    int error;

    pxsdr_usb_device **device_list;
    if ((error = pxsdr_discover_devices(PXSDR.context, &device_list, true)) < 0)
        return;

    if (!device_list[0]) {
        fprintf(stderr, "No connected PXSDR devices detected.\n");
        return;
    }

    fprintf(stderr, "Available PXSDR devices:\n");

    for (unsigned i = 0; device_list[i]; ++i) {
        fprintf(stderr, "  #%u: %s using %s at %s bus %03d address %03d, serial number %s\n",
                device_list[i]->index,
                pxsdr_device_variant_string(device_list[i]->variant),
                pxsdr_device_mode_string(device_list[i]->mode),
                device_list[i]->usb_superspeed ? "USB3" : "USB2",
                device_list[i]->usb_bus, device_list[i]->usb_address,
                device_list[i]->serial);
    }

    pxsdr_free_device_list(device_list);
}

bool pxsdrOpen(void) {
    int error;
    if ((error = pxsdr_init(&PXSDR.context)) < 0) {
        fprintf(stderr, "pxsdr_init: %s\n", pxsdr_strerror(NULL, error));
        goto error;
    }

    if ((error = pxsdr_set_log_callback(PXSDR.context, &pxsdr_logger)) < 0) {
        fprintf(stderr, "pxsdr_set_log_callback: %s\n", pxsdr_strerror(NULL, error));
        goto error;
    }

    if (PXSDR.firmware_path) {
        if ((error = pxsdr_set_firmware_path(PXSDR.context, PXSDR.firmware_path)) < 0) {
            fprintf(stderr, "pxsdr_set_firmware_path: %s\n", pxsdr_strerror(PXSDR.context, error));
            goto error;
        }
    }

    if (Modes.dev_name) {
        if ((error = pxsdr_open_by_serial(PXSDR.context, Modes.dev_name, &PXSDR.dev)) < 0) {
            fprintf(stderr, "pxsdr_open_by_serial(%s): %s\n", Modes.dev_name, pxsdr_strerror(PXSDR.context, error));
            goto error;
        }
    } else {
        if ((error = pxsdr_open_single_device(PXSDR.context, &PXSDR.dev)) < 0) {
            fprintf(stderr, "pxsdr_open_single_device: %s\n", pxsdr_strerror(PXSDR.context, error));
            if (error == PXSDR_ERROR_MULTIPLE_DEVICES)
                fprintf(stderr, "Try using --device to select a specific device by serial number.\n");
            goto error;
        }
    }

    pxsdr_device_info *info = pxsdr_get_device_info(PXSDR.dev);
    fprintf(stderr, "PXSDR: connected to %s, serial %s, %s bus %03d address %03d\n",
            pxsdr_device_variant_string(info->variant),
            info->serial,
            info->usb_superspeed ? "USB3" : "USB2",
            info->usb_bus, info->usb_address);

    if ((error = pxsdr_set_buffering(PXSDR.dev, PXSDR_BUFFERS, PXSDR_BUFSIZE)) < 0) {
        fprintf(stderr, "pxsdr_set_buffering: %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_sampling_mode(PXSDR.dev, PXSDR_SAMPLING_MODE_COMPLEX_BASEBAND, PXSDR_SAMPLE_FORMAT_INT16)) < 0) {
        fprintf(stderr, "pxsdr_set_sampling_mode(COMPLEX_BASEBAND,INT16): %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_sample_rate(PXSDR.dev, Modes.sample_rate)) < 0) {
        fprintf(stderr, "pxsdr_set_sample_rate(%.0f): %s\n", Modes.sample_rate, pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_decimation(PXSDR.dev, PXSDR.decimation)) < 0) {
        fprintf(stderr, "pxsdr_set_decimation(%u): %s\n", PXSDR.decimation, pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_frequency(PXSDR.dev, Modes.freq)) < 0) {
        fprintf(stderr, "pxsdr_set_frequency(%d): %s\n", Modes.freq, pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_bandpass_filter(PXSDR.dev, -PXSDR.bandwidth/2, PXSDR.bandwidth/2)) < 0) {
        fprintf(stderr, "pxsdr_set_bandpass_filter(%d,%d): %s\n", -PXSDR.bandwidth/2, PXSDR.bandwidth/2, pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if ((error = pxsdr_set_gain(PXSDR.dev, Modes.gain / 10.0)) < 0) {
        fprintf(stderr, "pxsdr_set_gain: %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if (PXSDR.lna_gain >= 0 && (error = pxsdr_set_gain_stage(PXSDR.dev, PXSDR_GAIN_LNA, PXSDR.lna_gain)) < 0) {
        fprintf(stderr, "pxsdr_set_gain_stage(LNA): %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if (PXSDR.mixer_gain >= 0 && (error = pxsdr_set_gain_stage(PXSDR.dev, PXSDR_GAIN_MIXER, PXSDR.mixer_gain)) < 0) {
        fprintf(stderr, "pxsdr_set_gain_stage(MIXER): %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    if (PXSDR.vga_gain >= 0 && (error = pxsdr_set_gain_stage(PXSDR.dev, PXSDR_GAIN_VGA, PXSDR.vga_gain)) < 0) {
        fprintf(stderr, "pxsdr_set_gain_stage(VGA): %s\n", pxsdr_strerror(PXSDR.context, error));
        goto error;
    }

    float actual;
    unsigned lna, mixer, vga;
    pxsdr_get_gain_stage(PXSDR.dev, PXSDR_GAIN_LNA, &lna);
    pxsdr_get_gain_stage(PXSDR.dev, PXSDR_GAIN_MIXER, &mixer);
    pxsdr_get_gain_stage(PXSDR.dev, PXSDR_GAIN_VGA, &vga);

    pxsdr_get_actual_gain(PXSDR.dev, &actual);
    fprintf(stderr, "PXSDR: gain set to about %.1f dB (LNA %u, mixer %u, VGA %u)\n", actual, lna, mixer, vga);

    PXSDR.converter = init_converter(INPUT_SC16,
                                     Modes.sample_rate,
                                     Modes.dc_filter,
                                     &PXSDR.converter_state);
    if (!PXSDR.converter) {
        fprintf(stderr, "PXSDR: can't initialize sample converter\n");
        goto error;
    }

    return true;

 error:
    if (error == PXSDR_ERROR_MULTIPLE_DEVICES || error == PXSDR_ERROR_NOT_FOUND)
        pxsdrEnumerate();

    pxsdrCleanup();
    return false;
}

void pxsdrCleanup()
{
    if (PXSDR.converter) {
        cleanup_converter(PXSDR.converter_state);
        PXSDR.converter = NULL;
        PXSDR.converter_state = NULL;
    }

    if (PXSDR.firmware_path) {
        free(PXSDR.firmware_path);
        PXSDR.firmware_path = NULL;
    }

    if (PXSDR.dev) {
        pxsdr_close(PXSDR.dev);
        PXSDR.dev = NULL;
    }

    if (PXSDR.context) {
        pxsdr_exit(PXSDR.context);
        PXSDR.context = NULL;
    }
}

void pxsdrRun()
{
    if (!PXSDR.dev) {
        return;
    }

    PXSDR.expected_timestamp = 0;

    // run it all
    int error;
    if ((error = pxsdr_stream_data(PXSDR.dev, handle_one_buffer, /* user_data */ NULL, /* timeout_ms */ 1000)) < 0) {
        fprintf(stderr, "pxsdr_stream_data: %s\n", pxsdr_strerror(PXSDR.context, error));
        return;
    }

    // normal termination
}

void pxsdrClose()
{
    pxsdrCleanup();
}

static bool handle_one_buffer(pxsdr_sample_block *block, void *user_data)
{
    MODES_NOTUSED(user_data);

    uint64_t now = mstime();

    sdrMonitor();

    if (Modes.exit) {
        // ask our caller to return
        pxsdr_stop_streaming(PXSDR.dev);
        return true;
    }

    if (block->flags & PXSDR_STREAM_OUT_OF_RANGE) {
        //fprintf(stderr, "PXSDR: out of range samples seen\n");
    }

    unsigned offset = 0;
    while (offset < block->count) {
        /* get a new output buffer and fill in metadata */

        struct mag_buf *outbuf = fifo_acquire(0 /* don't wait */);
        if (!outbuf) {
            // drop remaining samples
            break;
        }

        outbuf->sampleTimestamp = (block->timestamp + offset) * 12000000 / Modes.sample_rate;
        outbuf->sysTimestamp = now - (block->count - offset) / (Modes.sample_rate / 1000);

        if (offset == 0 && (block->flags & PXSDR_STREAM_OVERRUN) != 0) {
            outbuf->flags = MAGBUF_DISCONTINUOUS;
            outbuf->dropped = (PXSDR.expected_timestamp < block->timestamp) ? (block->timestamp - PXSDR.expected_timestamp) : 0;
            fprintf(stderr, "PXSDR: %u samples dropped\n", outbuf->dropped);
        } else {
            outbuf->flags = 0;
            outbuf->dropped = 0;
        }

        /* process whatever fits into this buffer */
        unsigned out_avail = outbuf->totalLength - outbuf->overlap;
        unsigned in_avail = block->count - offset;
        unsigned copy_len = (in_avail < out_avail) ? in_avail : out_avail;
        PXSDR.converter(block->samples + offset, outbuf->data + outbuf->overlap, copy_len, PXSDR.converter_state, &outbuf->mean_level, &outbuf->mean_power);
        outbuf->validLength = outbuf->overlap + copy_len;
        offset += copy_len * 2;

        /* push buffer to the demodulator */
        fifo_enqueue(outbuf);
    }

    PXSDR.expected_timestamp = block->timestamp + block->count;
    return true;
}
