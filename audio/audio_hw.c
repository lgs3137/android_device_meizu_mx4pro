/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2013-2015 The CyanogenMod Project
 *               Daniel Hillenbrand <codeworkx@cyanogenmod.com>
 *               Gfuillaume "XpLoDWilD" Lesniak <xplodgui@gmail.com>
 * Copyright (c) 2015      Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2015      Tatsuyuki Ishi <ishitatsuyuki@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "audio_hw_primary"
#define LOG_NDEBUG 0
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/misc.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_route/audio_route.h>
#include "tfa/Tfa98xx.h"
#include "tfa/Tfa98xx_Registers.h"
#include "tfa/Tfa98xx_genregs.h"
#include "ril/ril_interface.h"
enum
{
    PCM_CARD = 0,
    PCM_CARD_HIFI,
    PCM_TOTAL
};
enum
{
    PCM_DEVICE_CAPTURE = 0,
    PCM_DEVICE_PLAYBACK,
    PCM_DEVICE_CODEC_MEDIA,
    PCM_DEVICE_BASEBAND,
    PCM_DEVICE_CODEC_VOICE,
    PCM_DEVICE_SCO,
    PCM_DEVICE_AMPLIFIER,
    PCM_DEVICE_TOTAL
};
#define MIXER_CARD 0
#define MIXER_CARD_HIFI 1
/* duration in ms of volume ramp applied when starting capture to remove plop */
#define CAPTURE_START_RAMP_MS 100
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
/*
 * Default sampling for HDMI multichannel output
 *
 * Maximum number of channel mask configurations supported. Currently the
 * primary output only supports 1 (stereo) and the
 * multi channel HDMI output 2 (5.1 and 7.1)
 */
#define HDMI_MAX_SUPPORTED_CHANNEL_MASKS 2
struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};
struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};
struct pcm_config pcm_config_sco = {
    .channels = 1,
    .rate = 8000,
    .period_size = 128,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};
/* Routing */
enum
{
    OUT_DEVICE_SPEAKER,
    OUT_DEVICE_EARPIECE,
    OUT_DEVICE_HEADSET,
    OUT_DEVICE_HEADPHONES,
    OUT_DEVICE_BT_SCO,
    OUT_DEVICE_TAB_SIZE, /* number of rows in route_configs[][] */
    OUT_DEVICE_NONE,
    OUT_DEVICE_CNT
};
enum
{
    IN_SOURCE_MIC,
    IN_SOURCE_VOICE_RECOGNITION,
    IN_SOURCE_VOICE_COMMUNICATION,
    IN_SOURCE_VOICE_CALL,
    IN_SOURCE_TAB_SIZE, /* number of lines in route_configs[][] */
    IN_SOURCE_NONE,
    IN_SOURCE_CNT
};
#define BACKEND_CODEC_MEDIA (1 << PCM_DEVICE_CODEC_MEDIA)
#define PCM_BIT_BASEBAND (1 << PCM_DEVICE_BASEBAND)
#define BACKEND_CODEC_VOICE (1 << PCM_DEVICE_CODEC_VOICE)
#define BACKEND_SPEAKER (1 << PCM_DEVICE_AMPLIFIER)
struct route_config
{
    const char *const output_route;
    const char *const input_route;
    const int pcm_devices;
};
const struct route_config voice_speaker = {
    "voice-call-speaker",
    "voice-call-main-mic",
    PCM_BIT_BASEBAND | BACKEND_CODEC_VOICE | BACKEND_SPEAKER,
};
const struct route_config voice_earpiece = {
    "voice-call-earphones",
    "voice-call-main-mic",
    PCM_BIT_BASEBAND | BACKEND_CODEC_VOICE,
};
const struct route_config voice_headphones = {
    "voice-call-headphones",
    "voice-call-main-mic",
    PCM_BIT_BASEBAND | BACKEND_CODEC_VOICE,
};
const struct route_config voice_headset = {
    "voice-call-headset",
    "voice-call-headset-mic",
    PCM_BIT_BASEBAND | BACKEND_CODEC_VOICE,
};
const struct route_config voice_bt_sco = {
    "voice-bt-sco-headset",
    "voice-bt-sco-headset-mic",
    PCM_BIT_BASEBAND | BACKEND_CODEC_VOICE,
};
const struct route_config media_speaker = {
    "media-speaker",
    "media-main-mic",
    BACKEND_CODEC_MEDIA | BACKEND_SPEAKER,
};
const struct route_config media_earpiece = {
    "media-earphones",
    "media-main-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config media_headphones = {
    "media-headphones",
    "media-main-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config media_headset = {
    "media-headphones",
    "media-headset-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config media_bt_sco = {
    "media-bt-sco-headset",
    "media-bt-sco-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config voice_rec_speaker = {
    "voice-rec-speaker",
    "voice-rec-main-mic",
    BACKEND_CODEC_MEDIA | BACKEND_SPEAKER,
};
const struct route_config voice_rec_headphones = {
    "voice-rec-headphones",
    "voice-rec-main-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config voice_rec_headset = {
    "voice-rec-headphones",
    "voice-rec-headset-mic",
    BACKEND_CODEC_MEDIA,
};
const struct route_config communication_speaker = {
    "communication-speaker",
    "communication-main-mic",
    BACKEND_CODEC_VOICE | BACKEND_SPEAKER,
};
const struct route_config communication_earpiece = {
    "communication-earphones",
    "communication-main-mic",
    BACKEND_CODEC_VOICE,
};
const struct route_config communication_headphones = {
    "communication-headphones",
    "communication-main-mic",
    BACKEND_CODEC_VOICE,
};
const struct route_config communication_headset = {
    "communication-headset",
    "communication-headset-mic",
    BACKEND_CODEC_VOICE,
};
const struct route_config none = {
    "none",
    "none",
    0,
};
const struct route_config *const route_configs[IN_SOURCE_TAB_SIZE][OUT_DEVICE_TAB_SIZE] = {
    {
        /* IN_SOURCE_MIC */
        &media_speaker,    /* OUT_DEVICE_SPEAKER */
        &media_earpiece,   /* OUT_DEVICE_EARPIECE */
        &media_headset,    /* OUT_DEVICE_HEADSET */
        &media_headphones, /* OUT_DEVICE_HEADPHONES */
        &media_bt_sco,     /* OUT_DEVICE_BT_SCO */
    },
    {
        /* IN_SOURCE_VOICE_RECOGNITION */
        &voice_rec_speaker,    /* OUT_DEVICE_SPEAKER */
        &none,                 /* OUT_DEVICE_EARPIECE */
        &voice_rec_headset,    /* OUT_DEVICE_HEADSET */
        &voice_rec_headphones, /* OUT_DEVICE_HEADPHONES */
        &media_bt_sco,         /* OUT_DEVICE_BT_SCO */
    },
    {
        /* IN_SOURCE_VOICE_COMMUNICATION */
        &communication_speaker,    /* OUT_DEVICE_SPEAKER */
        &communication_earpiece,   /* OUT_DEVICE_EARPIECE */
        &communication_headset,    /* OUT_DEVICE_HEADSET */
        &communication_headphones, /* OUT_DEVICE_HEADPHONES */
        &media_bt_sco,             /* OUT_DEVICE_BT_SCO */
    },
    {
        /* IN_SOURCE_VOICE_CALL */
        &voice_speaker,    /* OUT_DEVICE_SPEAKER */
        &voice_earpiece,   /* OUT_DEVICE_EARPIECE */
        &voice_headset,    /* OUT_DEVICE_HEADSET */
        &voice_headphones, /* OUT_DEVICE_HEADPHONES */
        &voice_bt_sco,     /* OUT_DEVICE_BT_SCO */
    },
};
/* TFA9890 */
struct tfa_param_data
{
    unsigned int size;
    unsigned int type;
    unsigned char *data;
};
enum
{
    TFA_PATCH_DSP = 0,
    TFA_PATCH_COLDBOOT,
    TFA_PATCH_MAX
};
enum
{
    TFA_TYPE_MUSIC_0 = 0,
    TFA_TYPE_MUSIC_1,
    TFA_TYPE_MUSIC_2,
    TFA_TYPE_SPEECH,
    TFA_TYPE_MAX
};
struct audio_device
{
    struct audio_hw_device hw_device;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    audio_devices_t out_device; /* "or" of stream_out.device for all active
                                   output streams */
    audio_devices_t in_device;
    bool mic_mute;
    struct audio_route *ar;
    audio_source_t input_source;
    int cur_route_id; /* current route ID: combination of input source
                           * and output device IDs */
    audio_mode_t mode;
    struct pcm *pcm_devices[PCM_DEVICE_TOTAL][2]; /* 0 = IN, 1 = OUT */
    struct mixer *mixer;
    /* SCO audio */
    struct pcm *pcm_sco_rx;
    struct pcm *pcm_sco_tx;
    float voice_volume;
    bool in_call;
    bool bluetooth_nrec;
    bool bypass_dsp;
    audio_channel_mask_t in_channel_mask;
    /* RIL */
    ril_handle_t ril_handle;
    struct stream_out *output;
    pthread_mutex_t lock_outputs; /* see note below on mutex acquisition order */
    /* TFA9890 */
    Tfa98xx_handle_t tfa_handle;
    struct tfa_param_data *tfa_patch_data[TFA_PATCH_MAX], *tfa_config_data, *tfa_speaker_data,
        *tfa_preset_data[TFA_TYPE_MAX], *tfa_eq_data[TFA_TYPE_MAX];
};
struct stream_out
{
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct pcm *pcm;
    struct pcm_config config;
    bool standby; /* true if all PCMs are inactive */
    audio_devices_t device;
    audio_channel_mask_t channel_mask;
    /* Array of supported channel mask configurations. +1 so that the last entry
     * is always 0 */
    audio_channel_mask_t supported_channel_masks[HDMI_MAX_SUPPORTED_CHANNEL_MASKS + 1];
    bool muted;
    uint64_t written; /* total frames written, not cleared when entering standby */
    struct audio_device *dev;
};
struct stream_in
{
    struct audio_stream_in stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct pcm *pcm;
    bool standby;
    unsigned int requested_rate;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    int read_status;
    audio_source_t input_source;
    audio_io_handle_t io_handle;
    audio_devices_t device;
    uint16_t ramp_vol;
    uint16_t ramp_step;
    size_t ramp_frames;
    audio_channel_mask_t channel_mask;
    audio_input_flags_t flags;
    struct pcm_config config;
    struct audio_device *dev;
};
#define STRING_TO_ENUM(string) \
    {                          \
        #string, string        \
    }
struct string_to_enum
{
    const char *name;
    uint32_t value;
};
const struct string_to_enum out_channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
};
static int get_output_device_id(audio_devices_t device)
{
    if(device == AUDIO_DEVICE_NONE) return OUT_DEVICE_NONE;
    if(popcount(device) != 1) return OUT_DEVICE_NONE;
    switch(device)
    {
        case AUDIO_DEVICE_OUT_SPEAKER:
            return OUT_DEVICE_SPEAKER;
        case AUDIO_DEVICE_OUT_EARPIECE:
            return OUT_DEVICE_EARPIECE;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
            return OUT_DEVICE_HEADSET;
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
            return OUT_DEVICE_HEADPHONES;
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
            return OUT_DEVICE_BT_SCO;
        default:
            return OUT_DEVICE_NONE;
    }
}
static int get_input_source_id(audio_source_t source)
{
    switch(source)
    {
        case AUDIO_SOURCE_DEFAULT:
            return IN_SOURCE_NONE;
        case AUDIO_SOURCE_MIC:
        case AUDIO_SOURCE_CAMCORDER:
            return IN_SOURCE_MIC;
        case AUDIO_SOURCE_VOICE_RECOGNITION:
            return IN_SOURCE_VOICE_RECOGNITION;
        case AUDIO_SOURCE_VOICE_COMMUNICATION:
            return IN_SOURCE_VOICE_COMMUNICATION;
        case AUDIO_SOURCE_VOICE_CALL:
            return IN_SOURCE_VOICE_CALL;
        default:
            return IN_SOURCE_NONE;
    }
}
static void do_out_standby(struct stream_out *out);
static void adev_set_call_audio_path(struct audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int start_tfa(struct audio_device *adev);
static int stop_tfa(struct audio_device *adev);
static int tfa_bypass_dsp_on(struct audio_device *adev);
static int tfa_bypass_dsp_off(struct audio_device *adev);
/**
 * NOTE: when multiple mutexes have to be acquired, always respect the
 * following order: hw device > in stream > out stream
 */
/* Helper functions */
static bool route_changed(struct audio_device *adev)
{
    int output_device_id = get_output_device_id(adev->out_device);
    int input_source_id = get_input_source_id(adev->input_source);
    int new_route_id;
    new_route_id = (1 << (input_source_id + OUT_DEVICE_CNT)) + (1 << output_device_id);
    return new_route_id != adev->cur_route_id;
}
static void select_devices(struct audio_device *adev)
{
    int output_device_id = get_output_device_id(adev->out_device);
    int input_source_id = get_input_source_id(adev->input_source);
    const char *output_route = NULL;
    const char *input_route = NULL;
    int pcm_devices = 0;
    char current_device[64] = {0};
    int new_route_id;
    new_route_id = (1 << (input_source_id + OUT_DEVICE_CNT)) + (1 << output_device_id);
    if(new_route_id == adev->cur_route_id)
    {
        ALOGV("*** %s: Routing hasn't changed, leaving function.", __func__);
        return;
    }
    adev->cur_route_id = new_route_id;
    if(input_source_id != IN_SOURCE_NONE)
    {
        if(output_device_id != OUT_DEVICE_NONE)
        {
            input_route = route_configs[input_source_id][output_device_id]->input_route;
            output_route = route_configs[input_source_id][output_device_id]->output_route;
            pcm_devices = route_configs[input_source_id][output_device_id]->pcm_devices;
        }
        else
        {
            switch(adev->in_device)
            {
                case AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN:
                    output_device_id = OUT_DEVICE_HEADSET;
                    break;
                case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN:
                    output_device_id = OUT_DEVICE_BT_SCO;
                    break;
                default:
                    if(adev->input_source == AUDIO_SOURCE_VOICE_CALL)
                    {
                        output_device_id = OUT_DEVICE_EARPIECE;
                    }
                    else
                    {
                        output_device_id = OUT_DEVICE_SPEAKER;
                    }
                    break;
            }
            input_route = (route_configs[input_source_id][output_device_id])->input_route;
            pcm_devices = route_configs[input_source_id][output_device_id]->pcm_devices;
        }
    }
    else if(output_device_id != OUT_DEVICE_NONE)
    {
        output_route = (route_configs[IN_SOURCE_MIC][output_device_id])->output_route;
        pcm_devices = route_configs[IN_SOURCE_MIC][output_device_id]->pcm_devices;
    }
    ALOGV("***** %s: devices=%#x, input src=%d -> "
          "output route: %s, input route: %s",
          __func__,
          adev->out_device,
          adev->input_source,
          output_route ? output_route : "none",
          input_route ? input_route : "none");
    audio_route_reset(adev->ar);
    /*
     * Apply the new audio routes and set volumes
     */
    if(output_route != NULL)
    {
        audio_route_apply_path(adev->ar, output_route);
    }
    if(input_route != NULL)
    {
        audio_route_apply_path(adev->ar, input_route);
    }
    audio_route_update_mixer(adev->ar);
    /*
     * Apply PCM device power
     */
    if(pcm_devices & BACKEND_CODEC_MEDIA)
    {
        if(!adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0])
        {
            adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0] =
                pcm_open(PCM_CARD, PCM_DEVICE_CODEC_MEDIA, PCM_IN, &pcm_config_in);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0]);
            if(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0]))
            {
                ALOGE("pcm_open(CODEC_MEDIA) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0]);
            }
        }
        if(!adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1])
        {
            adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1] =
                pcm_open(PCM_CARD, PCM_DEVICE_CODEC_MEDIA, PCM_OUT, &pcm_config_out);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1]);
            if(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1]))
            {
                ALOGE("pcm_open(CODEC_MEDIA) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1]);
            }
        }
    }
    else
    {
        if(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0])
            pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0]);
        if(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1])
            pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1]);
        adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][0] = NULL;
        adev->pcm_devices[PCM_DEVICE_CODEC_MEDIA][1] = NULL;
    }
    if(pcm_devices & BACKEND_CODEC_VOICE)
    {
        if(!adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0])
        {
            adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0] =
                pcm_open(PCM_CARD, PCM_DEVICE_CODEC_VOICE, PCM_IN, &pcm_config_in);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0]);
            if(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0]))
            {
                ALOGE("pcm_open(CODEC_VOICE) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0]);
            }
        }
        if(!adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1])
        {
            adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1] =
                pcm_open(PCM_CARD, PCM_DEVICE_CODEC_VOICE, PCM_OUT, &pcm_config_out);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1]);
            if(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1]))
            {
                ALOGE("pcm_open(CODEC_VOICE) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1]);
            }
        }
    }
    else
    {
        if(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0])
            pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0]);
        if(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1])
            pcm_close(adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1]);
        adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][0] = NULL;
        adev->pcm_devices[PCM_DEVICE_CODEC_VOICE][1] = NULL;
    }
    if(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0] && adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1] &&
       !adev->in_call && adev->bypass_dsp)
    {
        tfa_bypass_dsp_off(adev);
        adev->bypass_dsp = false;
        ALOGV("Turned off Tfa9890 bypass-dsp");
    }
    if(pcm_devices & BACKEND_SPEAKER)
    {
        if(!adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0])
        {
            adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0] =
                pcm_open(PCM_CARD, PCM_DEVICE_AMPLIFIER, PCM_IN, &pcm_config_in);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0]);
            if(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0]))
            {
                ALOGE("pcm_open(AMPLIFIER) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0]);
            }
        }
        if(!adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1])
        {
            adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1] =
                pcm_open(PCM_CARD, PCM_DEVICE_AMPLIFIER, PCM_OUT, &pcm_config_out);
            pcm_prepare(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1]);
            if(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1] &&
               !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1]))
            {
                ALOGE("pcm_open(AMPLIFIER) failed: %s",
                      pcm_get_error(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1]));
                pcm_close(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1]);
            }
            start_tfa(adev);
        }
        if(adev->in_call && !adev->bypass_dsp)
        {
            tfa_bypass_dsp_on(adev);
            adev->bypass_dsp = true;
            ALOGV("Turned on Tfa9890 bypass-dsp");
        }
    }
    else
    {
        if(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0])
            pcm_close(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0]);
        if(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1])
        {
            pcm_close(adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1]);
            stop_tfa(adev);
        }
        adev->pcm_devices[PCM_DEVICE_AMPLIFIER][0] = NULL;
        adev->pcm_devices[PCM_DEVICE_AMPLIFIER][1] = NULL;
    }
}
/**********************************************************
 * BT SCO functions
 **********************************************************/
/* must be called with the hw device mutex locked, OK to hold other mutexes */
static void start_bt_sco(struct audio_device *adev)
{
    if(adev->pcm_sco_rx != NULL || adev->pcm_sco_tx != NULL)
    {
        ALOGW("%s: SCO PCMs already open!\n", __func__);
        return;
    }
    ALOGV("%s: Opening SCO PCMs", __func__);
    adev->pcm_sco_rx = pcm_open(PCM_CARD, PCM_DEVICE_SCO, PCM_OUT | PCM_MONOTONIC, &pcm_config_sco);
    if(adev->pcm_sco_rx != NULL && !pcm_is_ready(adev->pcm_sco_rx))
    {
        ALOGE("%s: cannot open PCM SCO RX stream: %s", __func__, pcm_get_error(adev->pcm_sco_rx));
        goto err_sco_rx;
    }
    adev->pcm_sco_tx = pcm_open(PCM_CARD, PCM_DEVICE_SCO, PCM_IN, &pcm_config_sco);
    if(adev->pcm_sco_tx && !pcm_is_ready(adev->pcm_sco_tx))
    {
        ALOGE("%s: cannot open PCM SCO TX stream: %s", __func__, pcm_get_error(adev->pcm_sco_tx));
        goto err_sco_tx;
    }
    pcm_start(adev->pcm_sco_rx);
    pcm_start(adev->pcm_sco_tx);
    return;
err_sco_tx:
    pcm_close(adev->pcm_sco_tx);
    adev->pcm_sco_tx = NULL;
err_sco_rx:
    pcm_close(adev->pcm_sco_rx);
    adev->pcm_sco_rx = NULL;
}
/* must be called with the hw device mutex locked, OK to hold other mutexes */
static void stop_bt_sco(struct audio_device *adev)
{
    ALOGV("%s: Closing SCO PCMs", __func__);
    if(adev->pcm_sco_rx != NULL)
    {
        pcm_stop(adev->pcm_sco_rx);
        pcm_close(adev->pcm_sco_rx);
        adev->pcm_sco_rx = NULL;
    }
    if(adev->pcm_sco_tx != NULL)
    {
        pcm_stop(adev->pcm_sco_tx);
        pcm_close(adev->pcm_sco_tx);
        adev->pcm_sco_tx = NULL;
    }
}
/**********************************************************
 * Marvell RIL functions
 **********************************************************/
static void start_call(struct audio_device *adev)
{
    if(adev->in_call) return;
    ril_open(&adev->ril_handle);
    adev->in_call = true;
    if(adev->out_device == AUDIO_DEVICE_NONE && adev->in_device == AUDIO_DEVICE_NONE)
    {
        ALOGV("%s: No device selected, use earpiece as the default", __func__);
        adev->out_device = AUDIO_DEVICE_OUT_EARPIECE;
    }
    adev->input_source = AUDIO_SOURCE_VOICE_CALL;
    select_devices(adev);
    adev->pcm_devices[PCM_DEVICE_BASEBAND][0] =
        pcm_open(PCM_CARD, PCM_DEVICE_BASEBAND, PCM_IN, &pcm_config_in);
    pcm_prepare(adev->pcm_devices[PCM_DEVICE_BASEBAND][0]);
    if(adev->pcm_devices[PCM_DEVICE_BASEBAND][0] &&
       !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_BASEBAND][0]))
    {
        ALOGE("pcm_open(BASEBAND) failed: %s",
              pcm_get_error(adev->pcm_devices[PCM_DEVICE_BASEBAND][0]));
        pcm_close(adev->pcm_devices[PCM_DEVICE_BASEBAND][0]);
    }
    adev->pcm_devices[PCM_DEVICE_BASEBAND][1] =
        pcm_open(PCM_CARD, PCM_DEVICE_BASEBAND, PCM_OUT, &pcm_config_out);
    pcm_prepare(adev->pcm_devices[PCM_DEVICE_BASEBAND][1]);
    if(adev->pcm_devices[PCM_DEVICE_BASEBAND][1] &&
       !pcm_is_ready(adev->pcm_devices[PCM_DEVICE_BASEBAND][1]))
    {
        ALOGE("pcm_open(BASEBAND) failed: %s",
              pcm_get_error(adev->pcm_devices[PCM_DEVICE_BASEBAND][1]));
        pcm_close(adev->pcm_devices[PCM_DEVICE_BASEBAND][1]);
    }
    adev_set_call_audio_path(adev);
    adev_set_voice_volume(&adev->hw_device, adev->voice_volume);
}
static void stop_call(struct audio_device *adev)
{
    if(!adev->in_call) return;
    ril_close(adev->ril_handle);
    if(adev->mode != AUDIO_MODE_IN_CALL)
    {
        /* Use speaker as the default. We do not want to stay in earpiece mode */
        if(adev->out_device == AUDIO_DEVICE_NONE || adev->out_device == AUDIO_DEVICE_OUT_EARPIECE)
        {
            adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
        }
        adev->input_source = AUDIO_SOURCE_DEFAULT;
        ALOGV("*** %s: Reset route to out devices=%#x, input src=%#x",
              __func__,
              adev->out_device,
              adev->input_source);
        select_devices(adev);
        pcm_close(adev->pcm_devices[PCM_DEVICE_BASEBAND][0]);
        pcm_close(adev->pcm_devices[PCM_DEVICE_BASEBAND][1]);
        adev->pcm_devices[PCM_DEVICE_BASEBAND][0] = NULL;
        adev->pcm_devices[PCM_DEVICE_BASEBAND][1] = NULL;
    }
    adev->in_call = false;
}
static void adev_set_call_audio_path(struct audio_device *adev)
{
    enum ril_path call_path;
    switch(adev->out_device)
    {
        default:
            call_path = RIL_PATH_SPEAKER;
    }
    ALOGV("RIL set path: %d", call_path);
    ril_setPath(adev->ril_handle, call_path);
}
/**********************************************************
 * TFA9890 functions
 **********************************************************/
static int start_tfa(struct audio_device *adev)
{
    enum Tfa98xx_Error err;
    unsigned short status;
    unsigned short coldboot;
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
    }
    coldboot = status & TFA98XX_STATUSREG_ACS_MSK;
    if(coldboot)
    {
        ALOGV("Tfa98xx cold startup");
    }
    else
    {
        ALOGV("Tfa98xx warm startup");
    }
    int ready = 0;
    int timeout;
    if(coldboot)
    {
        err = Tfa98xx_Init(adev->tfa_handle);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_Init failed %d\n", err);
            return -EINVAL;
        }
        static const int SAMPLE_RATE = 48000;
        err = Tfa98xx_SetSampleRate(adev->tfa_handle, SAMPLE_RATE);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_SetSampleRate failed %d", err);
        }
    }
    Tfa98xx_Powerdown(adev->tfa_handle, 0);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_Powerdown failed %d", err);
    }
    if(coldboot)
    {
        unsigned short dcdcRead = 0;
        unsigned short dcdcBoost = 0;
        /* set Max boost coil current 1.92 A */
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_DCDCBOOST, &dcdcRead);
        dcdcRead &= ~(TFA98XX_DCDCBOOST_DCMCC_MSK);
        dcdcRead |= (3 << TFA98XX_DCDCBOOST_DCMCC_POS);
        err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_DCDCBOOST, dcdcRead);
        /* set Max boost voltage 7.5 V */
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_DCDCBOOST, &dcdcBoost);
        dcdcBoost &= ~(TFA98XX_DCDCBOOST_DCVO_MSK);
        dcdcBoost |= 3;
        err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_DCDCBOOST, dcdcBoost);
        /* Check the PLL is powered up from status register 0 */
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
            return -EINVAL;
        }
        timeout = 0;
        while((status & TFA98XX_STATUSREG_AREFS_MSK) == 0)
        {
            /* not ready yet */
            err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
                return -EINVAL;
            }
            usleep(20);
            timeout++;
            if(timeout > 50)
            {
                ALOGV("Tfa98xx timeout: status %x", status);
                break;
            }
        }
    }
    else
    {
        err = Tfa98xx_SetMute(adev->tfa_handle, Tfa98xx_Mute_Amplifier);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_SetMute failed %d", err);
        }
    }
    /*  powered on
     *    - now it is allowed to access DSP specifics
     *    - stall DSP by setting reset
     */
    err = Tfa98xx_DspReset(adev->tfa_handle, 1);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_DspReset failed %d", err);
        return -EINVAL;
    }
    /*  wait until the DSP subsystem hardware is ready
     *    note that the DSP CPU is not running yet (RST=1)
     */
    timeout = 0;
    while(ready == 0)
    {
        /* are we ready? */
        err = Tfa98xx_DspSystemStable(adev->tfa_handle, &ready);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_DspSystemStable failed %d", err);
            return -EINVAL;
        }
        usleep(20);
        timeout++;
        if(timeout > 50)
        {
            ALOGV("Tfa98xx timeout: ready status %x", ready);
            break;
        }
    }
    if(coldboot)
    {
        unsigned short spkrCalibration;
        unsigned short mtp;
        int mtpChanged = 0;
        /* Patch the ROM code */
        Tfa98xx_DspPatch(adev->tfa_handle,
                         adev->tfa_patch_data[TFA_PATCH_COLDBOOT]->size,
                         adev->tfa_patch_data[TFA_PATCH_COLDBOOT]->data);
        Tfa98xx_DspPatch(adev->tfa_handle,
                         adev->tfa_patch_data[TFA_PATCH_DSP]->size,
                         adev->tfa_patch_data[TFA_PATCH_DSP]->data);
        /* Calibration */
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_SPKR_CALIBRATION, &spkrCalibration);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
            return -EINVAL;
        }
        static const int SPKR_CALIBRATION_EXTTS_VALUE = 26;
        spkrCalibration |= TFA98XX_SPKR_CALIBRATION_TROS_MSK;
        spkrCalibration &= ~(TFA98XX_SPKR_CALIBRATION_EXTTS_MSK);
        spkrCalibration |= (SPKR_CALIBRATION_EXTTS_VALUE << TFA98XX_SPKR_CALIBRATION_EXTTS_POS);
        err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_SPKR_CALIBRATION, spkrCalibration);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_WriteRegister16 failed %d", err);
            return -EINVAL;
        }
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_MTP, &mtp);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
            return -EINVAL;
        }
        static const int otcOn = 1;
        /* set reset MTPEX bit if needed */
        if((mtp & TFA98XX_MTP_MTPOTC) != otcOn)
        {
            /* need to change the OTC bit, set MTPEX=0 in any case */
            /* unlock key2 */
            err = Tfa98xx_WriteRegister16(adev->tfa_handle, 0x0B, 0x5A);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_WriteRegister16 failed %d", err);
                return -EINVAL;
            }
            /* MTPOTC=otcOn, MTPEX=0 */
            err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_MTP, (unsigned short)otcOn);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_WriteRegister16 failed %d", err);
                return -EINVAL;
            }
            /* CIMTP=1 */
            err = Tfa98xx_WriteRegister16(adev->tfa_handle, 0x62, 1 << 11);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_WriteRegister16 failed %d", err);
                return -EINVAL;
            }
            mtpChanged = 1;
        }
        timeout = 0;
        do
        {
            usleep(20);
            err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
                return -EINVAL;
            }
            timeout++;
            if(timeout > 50)
            {
                ALOGV("Tfa98xx timeout: status %x", status);
                break;
            }
        } while((status & TFA98XX_STATUSREG_MTPB_MSK) == TFA98XX_STATUSREG_MTPB_MSK);
        /* Check if MTPEX bit is set for calibration once mode */
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_MTP, &mtp);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
        }
        if(mtp & (1 << 1))
        {
            ALOGV("Tfa98xx already calibrated, skipping");
        }
        else
        {
            ALOGV("Tfa98xx not calibrated: calibration will start");
            err = Tfa98xx_SetMute(adev->tfa_handle, Tfa98xx_Mute_Digital);
            if(err != Tfa98xx_Error_Ok)
            {
                ALOGE("Tfa98xx_SetMute failed %d", err);
                return -EINVAL;
            }
        }
    }
    /* FIXME: Only change config when needed */
    if(coldboot || 1)
    {
        Tfa98xx_DspWriteSpeakerParameters(
            adev->tfa_handle, adev->tfa_speaker_data->size, adev->tfa_speaker_data->data);
        Tfa98xx_DspWriteConfig(
            adev->tfa_handle, adev->tfa_config_data->size, adev->tfa_config_data->data);
        /* FIXME: Get stream type dynamically */
        Tfa98xx_DspWritePreset(adev->tfa_handle,
                               adev->tfa_preset_data[TFA_TYPE_MUSIC_1]->size,
                               adev->tfa_preset_data[TFA_TYPE_MUSIC_1]->data);
        char *eq_data = (char *)adev->tfa_eq_data[TFA_TYPE_MUSIC_1]->data;
        for(int i = 0; i < 10; i++)
        {
            int index, offset;
            float b0, b1, b2, a0, a1;
            sscanf(eq_data, "%d %f %f %f %f %f%n", &index, &b0, &b1, &b2, &a0, &a1, &offset);
            if(index != (i + 1))
            {
                ALOGE("Invalid EQ data");
                return -EINVAL;
            }
            if(b0 == 0 || b1 == 0 || b2 == 0 || a0 == 0 || a1 == 0)
            {
                Tfa98xx_DspBiquad_Disable(adev->tfa_handle, index);
            }
            else
            {
                Tfa98xx_DspBiquad_SetCoeff(adev->tfa_handle, index, b0, b1, b2, a0, a1);
            }
            eq_data += offset;
        }
        err = Tfa98xx_SelectChannel(adev->tfa_handle, Tfa98xx_Channel_L_R);
    }
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_SelectChannel failed");
        return -EINVAL;
    }
    if(coldboot)
    {
        unsigned short mtp;
        unsigned short spkrCalibration;
        /* all settings loaded, signal the DSP to start calibration */
        err = Tfa98xx_SetConfigured(adev->tfa_handle);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_SetConfigured failed");
            return -EINVAL;
        }
        int tries = 0;
        static const int WAIT_TRIES = 1000;
        int calibrateDone;
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_MTP, &mtp);
        /* in case of calibrate once wait for MTPEX */
        if(mtp & TFA98XX_MTP_MTPOTC)
        {
            while((calibrateDone == 0) && (tries < WAIT_TRIES))
            {
                err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_MTP, &mtp);
                /* check MTP bit1 (MTPEX) */
                calibrateDone = (mtp & TFA98XX_MTP_MTPEX);
                tries++;
            }
        }
        else
        { /* poll xmem for calibrate always */
            while((calibrateDone == 0) && (tries < WAIT_TRIES))
            {
                /* TODO optimise with wait estimation */
                err = Tfa98xx_DspReadMem(adev->tfa_handle, 231, 1, &calibrateDone);
                tries++;
            }
            if(tries == WAIT_TRIES)
            {
                ALOGE("Tfa98xx calibration timeout");
            }
        }
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_SPKR_CALIBRATION, &spkrCalibration);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed %d", err);
        }
        spkrCalibration &= ~(TFA98XX_SPKR_CALIBRATION_TROS_MSK);
        err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_SPKR_CALIBRATION, spkrCalibration);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_WriteRegister16 failed %d", err);
        }
        if(!calibrateDone)
        {
            err = Tfa98xx_Powerdown(adev->tfa_handle, 1);
            return -EINVAL;
        }
    }
    err = Tfa98xx_SetMute(adev->tfa_handle, Tfa98xx_Mute_Off);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_SetMute failed %d", err);
        return -EINVAL;
    }
    return 0;
}
static int stop_tfa(struct audio_device *adev)
{
    ALOGV("Tfa98xx shutdown");
    enum Tfa98xx_Error err;
    unsigned short status = 0;
    int timeout;
    err = Tfa98xx_SetMute(adev->tfa_handle, Tfa98xx_Mute_Amplifier);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_SetMute failed");
    }
    usleep(33);
    /* NXP SL: Added checking if amplifier is still switching or not to avoid pop sound */
    /* now wait for the amplifier to turn off */
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_ReadRegister16 failed");
    }
    timeout = 0;
    while((status & TFA98XX_STATUSREG_SWS_MSK) == TFA98XX_STATUSREG_SWS_MSK)
    {
        err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_STATUSREG, &status);
        if(err != Tfa98xx_Error_Ok)
        {
            ALOGE("Tfa98xx_ReadRegister16 failed");
        }
        timeout++;
        if(timeout > 50)
        {
            ALOGV("Tfa98xx SWS checking timeout");
            break;
        }
    }
    err = Tfa98xx_Powerdown(adev->tfa_handle, 1);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_Powerdown failed");
    }
    return 0;
}
static int tfa_bypass_dsp_on(struct audio_device *adev)
{
    enum Tfa98xx_Error err = Tfa98xx_Error_Ok;
    unsigned short i2SRead = 0;
    unsigned short sysRead = 0;
    unsigned short sysCtrlRead = 0;
    unsigned short batProtRead = 0;
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_I2SREG, &i2SRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_I2S_SEL_REG, &sysRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_SYS_CTRL, &sysCtrlRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_BAT_PROT, &batProtRead);
    i2SRead &= ~(TFA98XX_I2SREG_CHSA_MSK);      /* Set CHSA to bypass DSP */
    sysRead &= ~(TFA98XX_I2S_SEL_REG_DCFG_MSK); /* Set DCDC compensation to
                                                     off */
    sysRead |= TFA98XX_I2S_SEL_REG_SPKR_MSK;    /* Set impedance as 8ohm */
    sysCtrlRead &= ~(TFA98XX_SYS_CTRL_DCA_MSK); /* Set DCDC to follower
                                                     mode */
    sysCtrlRead &= ~(TFA98XX_SYS_CTRL_CFE_MSK); /* Disable coolflux */
    batProtRead |= TFA989X_BAT_PROT_BSSBY_MSK;  /* Set clipper bypassed */
    /* Set CHSA to bypass DSP */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_I2SREG, i2SRead);
    /* Set DCDC compensation to off and set impedance as 8ohm */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_I2S_SEL_REG, sysRead);
    /* Set DCDC to follower mode and disable coolflux  */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_SYS_CTRL, sysCtrlRead);
    /* Set bypass clipper battery protection */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_BAT_PROT, batProtRead);
    return err;
}
static int tfa_bypass_dsp_off(struct audio_device *adev)
{
    enum Tfa98xx_Error err = Tfa98xx_Error_Ok;
    unsigned short i2SRead = 0;
    unsigned short sysRead = 0;
    unsigned short sysCtrlRead = 0;
    unsigned short batProtRead = 0;
    /* basic settings for quickset */
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_I2SREG, &i2SRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_I2S_SEL_REG, &sysRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_SYS_CTRL, &sysCtrlRead);
    err = Tfa98xx_ReadRegister16(adev->tfa_handle, TFA98XX_BAT_PROT, &batProtRead);
    i2SRead |= TFA98XX_I2SREG_CHSA;               /* Set CHSA to Unbypass DSP */
    sysRead |= TFA9890_I2S_SEL_REG_POR;           /* Set I2S SEL REG to set
                                               DCDC compensation to default 100%*/
    sysRead &= ~(TFA98XX_I2S_SEL_REG_SPKR_MSK);   /*Set impedance to be
                                                       defined by DSP */
    sysCtrlRead |= TFA98XX_SYS_CTRL_DCA_MSK;      /* Set DCDC to active mode*/
    sysCtrlRead |= TFA98XX_SYS_CTRL_CFE_MSK;      /* Enable Coolflux */
    batProtRead &= ~(TFA989X_BAT_PROT_BSSBY_MSK); /*Set clipper active */
    /* Set CHSA to Unbypass DSP */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_I2SREG, i2SRead);
    /* Set I2S SEL REG to set DCDC compensation to default 100% and
    Set impedance to be defined by DSP */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_I2S_SEL_REG, sysRead);
    /* Set DCDC to active mode and enable Coolflux */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_SYS_CTRL, sysCtrlRead);
    /* Set bypass clipper battery protection */
    err = Tfa98xx_WriteRegister16(adev->tfa_handle, TFA98XX_BAT_PROT, batProtRead);
    return err;
}
/* must be called with hw device outputs list, output stream, and hw device
 * mutexes locked */
static int start_output_stream(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    ALOGV("%s: starting stream", __func__);
    if(out->device &
       (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_WIRED_HEADSET |
        AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_DEVICE_OUT_ALL_SCO))
    {
        out->pcm = pcm_open(PCM_CARD, PCM_DEVICE_PLAYBACK, PCM_OUT | PCM_MONOTONIC, &out->config);
        if(out->pcm && !pcm_is_ready(out->pcm))
        {
            ALOGE("pcm_open(PLAYBACK) failed: %s", pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            return -ENOMEM;
        }
    }
    /* in call routing must go through set_parameters */
    if(!adev->in_call)
    {
        adev->out_device |= out->device;
        select_devices(adev);
    }
    ALOGV("%s: stream out device: %d, actual: %d", __func__, out->device, adev->out_device);
    return 0;
}
/* must be called with input stream and hw device mutexes locked */
static int start_input_stream(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    in->pcm = pcm_open(PCM_CARD, PCM_DEVICE_CAPTURE, PCM_IN, &in->config);
    if(in->pcm && !pcm_is_ready(in->pcm))
    {
        ALOGE("pcm_open() failed: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }
    /* if no supported sample rate is available, use the resampler */
    if(in->resampler)
    {
        in->resampler->reset(in->resampler);
    }
    in->frames_in = 0;
    /* in call routing must go through set_parameters */
    if(!adev->in_call)
    {
        adev->input_source = in->input_source;
        adev->in_device = in->device;
        adev->in_channel_mask = in->channel_mask;
        select_devices(adev);
    }
    /* initialize volume ramp */
    in->ramp_frames = (CAPTURE_START_RAMP_MS * in->requested_rate) / 1000;
    in->ramp_step = (uint16_t)(USHRT_MAX / in->ramp_frames);
    in->ramp_vol = 0;
    return 0;
}
static size_t get_input_buffer_size(unsigned int sample_rate, audio_format_t format,
                                    unsigned int channel_count)
{
    const struct pcm_config *config = &pcm_config_in;
    size_t size;
    /*
     * take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size = (config->period_size * sample_rate) / config->rate;
    size = ((size + 15) / 16) * 16;
    return size * channel_count * audio_bytes_per_sample(format);
}
static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer *buffer)
{
    struct stream_in *in;
    size_t i;
    if(buffer_provider == NULL || buffer == NULL)
    {
        return -EINVAL;
    }
    in = (struct stream_in *)((char *)buffer_provider - offsetof(struct stream_in, buf_provider));
    if(in->pcm == NULL)
    {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }
    if(in->frames_in == 0)
    {
        in->read_status = pcm_read(
            in->pcm, (void *)in->buffer, pcm_frames_to_bytes(in->pcm, in->config.period_size));
        if(in->read_status != 0)
        {
            ALOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
        /* Do stereo to mono conversion in place by discarding right channel */
        if(in->channel_mask == AUDIO_CHANNEL_IN_MONO)
            for(i = 1; i < in->frames_in; i++) in->buffer[i] = in->buffer[i * 2];
    }
    buffer->frame_count =
        (buffer->frame_count > in->frames_in) ? in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer +
                  (in->config.period_size - in->frames_in) *
                      audio_channel_count_from_in_mask(in->channel_mask);
    return in->read_status;
}
static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer *buffer)
{
    struct stream_in *in;
    if(buffer_provider == NULL || buffer == NULL) return;
    in = (struct stream_in *)((char *)buffer_provider - offsetof(struct stream_in, buf_provider));
    in->frames_in -= buffer->frame_count;
}
/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer
 * specified */
static ssize_t read_frames(struct stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    size_t frame_size = audio_stream_in_frame_size(&in->stream);
    while(frames_wr < frames)
    {
        size_t frames_rd = frames - frames_wr;
        if(in->resampler != NULL)
        {
            in->resampler->resample_from_provider(
                in->resampler, (int16_t *)((char *)buffer + frames_wr * frame_size), &frames_rd);
        }
        else
        {
            struct resampler_buffer buf = {
                {
                  raw : NULL,
                },
                frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if(buf.raw != NULL)
            {
                memcpy(
                    (char *)buffer + frames_wr * frame_size, buf.raw, buf.frame_count * frame_size);
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if(in->read_status != 0) return in->read_status;
        frames_wr += frames_rd;
    }
    return frames_wr;
}
/* API functions */
static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    return out->config.rate;
}
static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return -ENOSYS;
}
static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    return out->config.period_size *
           audio_stream_out_frame_size((const struct audio_stream_out *)stream);
}
static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    return out->channel_mask;
}
static audio_format_t out_get_format(const struct audio_stream *stream __unused)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}
static int out_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}
/* must be called with hw device outputs list, all out streams, and hw device
 * mutex locked */
static void do_out_standby(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    ALOGV("%s: output standby: %d", __func__, out->standby);
    if(!out->standby)
    {
        if(out->pcm)
        {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        if(out->device & AUDIO_DEVICE_OUT_ALL_SCO) stop_bt_sco(adev);
        out->standby = true;
        /* in call routing must go through set_parameters */
        if(!adev->in_call)
        {
            /* re-calculate the set of active devices from other streams */
            adev->out_device = AUDIO_DEVICE_NONE;
            /* Still reset the mixer to power down devices */
            select_devices(adev);
        }
    }
}
/* lock outputs list, stream, and device */
static void lock_output(struct audio_device *adev)
{
    pthread_mutex_lock(&adev->lock_outputs);
    struct stream_out *out = adev->output;
    if(out) pthread_mutex_lock(&adev->output->lock);
    pthread_mutex_lock(&adev->lock);
}
/* unlock device, stream, and outputs list
 */
static void unlock_output(struct audio_device *adev, struct stream_out *except)
{
    pthread_mutex_unlock(&adev->lock);
    struct stream_out *out = adev->output;
    if(out && out != except) pthread_mutex_unlock(&adev->output->lock);
    pthread_mutex_unlock(&adev->lock_outputs);
}
static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    lock_output(adev);
    do_out_standby(out);
    unlock_output(adev, NULL);
    return 0;
}
static int out_dump(const struct audio_stream *stream __unused, int fd __unused)
{
    return 0;
}
static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;
    ALOGV("%s: key value pairs: %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if(ret >= 0)
    {
        val = atoi(value);
        lock_output(adev);
        if((out->device != val) && (val != 0))
        {
            /* force output standby to start or stop SCO pcm stream if needed */
            if((val & AUDIO_DEVICE_OUT_ALL_SCO) ^ (out->device & AUDIO_DEVICE_OUT_ALL_SCO))
            {
                do_out_standby(out);
            }
            out->device = val;
            adev->out_device = val;
            select_devices(adev);
            /* start SCO stream if needed */
            if(val & AUDIO_DEVICE_OUT_ALL_SCO) start_bt_sco(adev);
        }
        unlock_output(adev, NULL);
    }
    str_parms_destroy(parms);
    return ret;
}
/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    const char *str;
    char value[256];
    struct str_parms *reply = str_parms_create();
    size_t i, j;
    int ret;
    bool first = true;
    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if(ret >= 0)
    {
        value[0] = '\0';
        i = 0;
        /* the last entry in supported_channel_masks[] is always 0 */
        while(out->supported_channel_masks[i] != 0)
        {
            for(j = 0; j < ARRAY_SIZE(out_channels_name_to_enum_table); j++)
            {
                if(out_channels_name_to_enum_table[j].value == out->supported_channel_masks[i])
                {
                    if(!first)
                    {
                        strcat(value, "|");
                    }
                    strcat(value, out_channels_name_to_enum_table[j].name);
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        str = str_parms_to_str(reply);
    }
    else
    {
        str = keys;
    }
    str_parms_destroy(query);
    str_parms_destroy(reply);
    return strdup(str);
}
static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    return (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
}
static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    if(out->device & AUDIO_DEVICE_OUT_EARPIECE)
    {
        struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, "EPOUT Digital Volume");
        int volume;
        volume = (int)(left * 256);
        mixer_ctl_set_value(ctl, 0, volume);
        return 0;
    }
    if(out->device & AUDIO_DEVICE_OUT_SPEAKER)
    {
        Tfa98xx_SetVolume(adev->tfa_handle, left);
        return 0;
    }
    if(out->device & (AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_WIRED_HEADSET))
    {
        struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, "HPOUT Digital Volume");
        int volume[2];
        volume[0] = (int)(left * 256);
        volume[1] = (int)(right * 256);
        mixer_ctl_set_array(ctl, volume, sizeof(volume) / sizeof(volume[0]));
        return 0;
    }
    return -ENOSYS;
}
static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    /* FIXME This comment is no longer correct
     * acquiring hw device mutex systematically is useful if a low
     * priority thread is waiting on the output stream mutex - e.g.
     * executing out_set_parameters() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&out->lock);
    if(out->standby)
    {
        pthread_mutex_unlock(&out->lock);
        lock_output(adev);
        if(!out->standby)
        {
            unlock_output(adev, out);
            goto false_alarm;
        }
        ret = start_output_stream(out);
        if(ret < 0)
        {
            unlock_output(adev, NULL);
            goto final_exit;
        }
        out->standby = false;
        unlock_output(adev, out);
    }
false_alarm:
    if(out->muted) memset((void *)buffer, 0, bytes);
    if(out->pcm)
    {
        ret = pcm_write(out->pcm, (void *)buffer, bytes);
    }
    if(ret == 0) out->written += bytes / (out->config.channels * sizeof(short));
exit:
    pthread_mutex_unlock(&out->lock);
final_exit:
    if(ret != 0)
    {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common));
    }
    return bytes;
}
static int out_get_render_position(const struct audio_stream_out *stream __unused,
                                   uint32_t *dsp_frames __unused)
{
    return -EINVAL;
}
static int out_add_audio_effect(const struct audio_stream *stream __unused,
                                effect_handle_t effect __unused)
{
    return 0;
}
static int out_remove_audio_effect(const struct audio_stream *stream __unused,
                                   effect_handle_t effect __unused)
{
    return 0;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *stream __unused,
                                        int64_t *timestamp __unused)
{
    return -EINVAL;
}
static int out_get_presentation_position(const struct audio_stream_out *stream, uint64_t *frames,
                                         struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -1;
    pthread_mutex_lock(&out->lock);
    int i;
    // There is a question how to implement this correctly when there is more
    // than one PCM stream.
    // We are just interested in the frames pending for playback in the kernel
    // buffer here,
    // not the total played since start.  The current behavior should be safe
    // because the
    // cases where both cards are active are marginal.
    if(out->pcm)
    {
        size_t avail;
        if(pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0)
        {
            size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
            // FIXME This calculation is incorrect if there is buffering
            // after app processor
            int64_t signed_frames = out->written - kernel_buffer_size + avail;
            // It would be unusual for this value to be negative, but check
            // just in case ...
            if(signed_frames >= 0)
            {
                *frames = signed_frames;
                ret = 0;
            }
        }
    }
    pthread_mutex_unlock(&out->lock);
    return ret;
}
/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    return in->requested_rate;
}
static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}
static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    return in->channel_mask;
}
static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    return get_input_buffer_size(in->requested_rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 audio_channel_count_from_in_mask(in_get_channels(stream)));
}
static audio_format_t in_get_format(const struct audio_stream *stream __unused)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}
static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}
/* must be called with in stream and hw device mutex locked */
static void do_in_standby(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    if(!in->standby)
    {
        pcm_close(in->pcm);
        in->pcm = NULL;
        if(adev->mode != AUDIO_MODE_IN_CALL)
        {
            in->dev->input_source = AUDIO_SOURCE_DEFAULT;
            in->dev->in_device = AUDIO_DEVICE_NONE;
            in->dev->in_channel_mask = 0;
            select_devices(adev);
        }
        in->standby = true;
    }
}
static int in_standby(struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    pthread_mutex_lock(&in->lock);
    pthread_mutex_lock(&in->dev->lock);
    do_in_standby(in);
    pthread_mutex_unlock(&in->dev->lock);
    pthread_mutex_unlock(&in->lock);
    return 0;
}
static int in_dump(const struct audio_stream *stream __unused, int fd __unused)
{
    return 0;
}
static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;
    bool apply_now = false;
    parms = str_parms_create_str(kvpairs);
    pthread_mutex_lock(&in->lock);
    pthread_mutex_lock(&adev->lock);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));
    if(ret >= 0)
    {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if((in->input_source != val) && (val != 0))
        {
            in->input_source = val;
            apply_now = !in->standby;
        }
    }
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if(ret >= 0)
    {
        /* strip AUDIO_DEVICE_BIT_IN to allow bitwise comparisons */
        val = atoi(value) & ~AUDIO_DEVICE_BIT_IN;
        /* no audio device uses val == 0 */
        if((in->device != val) && (val != 0))
        {
            /* force output standby to start or stop SCO pcm stream if needed */
            if((val & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) ^
               (in->device & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET))
            {
                do_in_standby(in);
            }
            in->device = val;
            apply_now = !in->standby;
        }
    }
    if(apply_now)
    {
        adev->input_source = in->input_source;
        adev->in_device = in->device;
        select_devices(adev);
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);
    str_parms_destroy(parms);
    return ret;
}
static char *in_get_parameters(const struct audio_stream *stream __unused,
                               const char *keys __unused)
{
    return strdup("");
}
static int in_set_gain(struct audio_stream_in *stream __unused, float gain __unused)
{
    return 0;
}
static void in_apply_ramp(struct stream_in *in, int16_t *buffer, size_t frames)
{
    size_t i;
    uint16_t vol = in->ramp_vol;
    uint16_t step = in->ramp_step;
    frames = (frames < in->ramp_frames) ? frames : in->ramp_frames;
    if(in->channel_mask == AUDIO_CHANNEL_IN_MONO)
        for(i = 0; i < frames; i++)
        {
            buffer[i] = (int16_t)((buffer[i] * vol) >> 16);
            vol += step;
        }
    else
        for(i = 0; i < frames; i++)
        {
            buffer[2 * i] = (int16_t)((buffer[2 * i] * vol) >> 16);
            buffer[2 * i + 1] = (int16_t)((buffer[2 * i + 1] * vol) >> 16);
            vol += step;
        }
    in->ramp_vol = vol;
    in->ramp_frames -= frames;
}
static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    int ret = 0;
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_in_frame_size(stream);
    /*
     * acquiring hw device mutex systematically is useful if a low
     * priority thread is waiting on the input stream mutex - e.g.
     * executing in_set_parameters() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&in->lock);
    if(in->standby)
    {
        pthread_mutex_lock(&adev->lock);
        ret = start_input_stream(in);
        pthread_mutex_unlock(&adev->lock);
        if(ret < 0) goto exit;
        in->standby = false;
    }
    /*if (in->num_preprocessors != 0)
        ret = process_frames(in, buffer, frames_rq);
      else */
    ret = read_frames(in, buffer, frames_rq);
    if(ret > 0) ret = 0;
    if(in->ramp_frames > 0) in_apply_ramp(in, buffer, frames_rq);
    /*
     * Instead of writing zeroes here, we could trust the hardware
     * to always provide zeroes when muted.
     */
    if(ret == 0 && adev->mic_mute) memset(buffer, 0, bytes);
exit:
    if(ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));
    pthread_mutex_unlock(&in->lock);
    return bytes;
}
static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
{
    return 0;
}
static int in_add_audio_effect(const struct audio_stream *stream __unused,
                               effect_handle_t effect __unused)
{
    return 0;
}
static int in_remove_audio_effect(const struct audio_stream *stream __unused,
                                  effect_handle_t effect __unused)
{
    return 0;
}
static int adev_open_output_stream(struct audio_hw_device *dev, audio_io_handle_t handle __unused,
                                   audio_devices_t devices, audio_output_flags_t flags __unused,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int ret;
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if(!out) return -ENOMEM;
    out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    if(devices == AUDIO_DEVICE_NONE) devices = AUDIO_DEVICE_OUT_SPEAKER;
    out->device = devices;
    out->config = pcm_config_out;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->dev = adev;
    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);
    out->standby = true;
    /* out->muted = false; by calloc() */
    /* out->written = 0; by calloc() */
    pthread_mutex_lock(&adev->lock_outputs);
    if(adev->output)
    {
        pthread_mutex_unlock(&adev->lock_outputs);
        ret = -EBUSY;
        goto err_open;
    }
    adev->output = out;
    pthread_mutex_unlock(&adev->lock_outputs);
    *stream_out = &out->stream;
    return 0;
err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}
static void adev_close_output_stream(struct audio_hw_device *dev, struct audio_stream_out *stream)
{
    struct audio_device *adev;
    out_standby(&stream->common);
    adev = (struct audio_device *)dev;
    pthread_mutex_lock(&adev->lock_outputs);
    if(adev->output == (struct stream_out *)stream)
    {
        adev->output = NULL;
    }
    pthread_mutex_unlock(&adev->lock_outputs);
    free(stream);
}
static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if(ret >= 0)
    {
        if(strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
        {
            adev->bluetooth_nrec = true;
        }
        else
        {
            adev->bluetooth_nrec = false;
        }
    }
    str_parms_destroy(parms);
    return ret;
}
static char *adev_get_parameters(const struct audio_hw_device *dev __unused,
                                 const char *keys __unused)
{
    return strdup("");
}
static int adev_init_check(const struct audio_hw_device *dev __unused)
{
    return 0;
}
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct audio_device *adev = (struct audio_device *)dev;
    adev->voice_volume = volume;
    if(adev->mode == AUDIO_MODE_IN_CALL)
    {
        ALOGV("RIL set volume: %f", volume);
        ril_setVolume(adev->ril_handle, volume);
    }
    return 0;
}
static int adev_set_master_volume(struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}
static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct audio_device *adev = (struct audio_device *)dev;
    if(adev->mode == mode)
    {
        return 0;
    }
    pthread_mutex_lock(&adev->lock);
    adev->mode = mode;
    if(adev->mode == AUDIO_MODE_IN_CALL)
    {
        ALOGV("*** %s: Entering IN_CALL mode", __func__);
        start_call(adev);
    }
    else
    {
        ALOGV("*** %s: Leaving IN_CALL mode", __func__);
        stop_call(adev);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}
static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct audio_device *adev = (struct audio_device *)dev;
    if(adev->in_call)
    {
        ril_setMute(adev->ril_handle, state);
    }
    adev->mic_mute = state;
    return 0;
}
static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct audio_device *adev = (struct audio_device *)dev;
    *state = adev->mic_mute;
    return 0;
}
static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                         const struct audio_config *config)
{
    return get_input_buffer_size(config->sample_rate,
                                 config->format,
                                 audio_channel_count_from_in_mask(config->channel_mask));
}
static int adev_open_input_stream(struct audio_hw_device *dev, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config *config,
                                  struct audio_stream_in **stream_in, audio_input_flags_t flags,
                                  const char *address __unused, audio_source_t source __unused)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in *in;
    int ret;
    *stream_in = NULL;
    /* Respond with a request for stereo if a different format is given. */
    if(config->channel_mask != AUDIO_CHANNEL_IN_STEREO)
    {
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
        return -EINVAL;
    }
    in = (struct stream_in *)calloc(1, sizeof(struct stream_in));
    if(in == NULL)
    {
        return -ENOMEM;
    }
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->dev = adev;
    in->standby = true;
    in->requested_rate = config->sample_rate;
    in->input_source = AUDIO_SOURCE_DEFAULT;
    /* strip AUDIO_DEVICE_BIT_IN to allow bitwise comparisons */
    in->device = devices & ~AUDIO_DEVICE_BIT_IN;
    in->io_handle = handle;
    in->channel_mask = config->channel_mask;
    in->flags = flags;
    in->config = pcm_config_in;
    in->buffer = malloc(in->config.period_size * in->config.channels *
                        audio_stream_in_frame_size(&in->stream));
    if(!in->buffer)
    {
        ret = -ENOMEM;
        goto err_malloc;
    }
    if(in->requested_rate != in->config.rate)
    {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               audio_channel_count_from_in_mask(in->channel_mask),
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        if(ret != 0)
        {
            ret = -EINVAL;
            goto err_resampler;
        }
        ALOGV("%s: Created resampler converting %d -> %d\n",
              __func__,
              in->config.rate,
              in->requested_rate);
    }
    ALOGV("%s: Requesting input stream with rate: %d, channels: 0x%x\n",
          __func__,
          config->sample_rate,
          config->channel_mask);
    *stream_in = &in->stream;
    return 0;
err_resampler:
    free(in->buffer);
err_malloc:
    free(in);
    return ret;
}
static void adev_close_input_stream(struct audio_hw_device *dev __unused,
                                    struct audio_stream_in *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    in_standby(&stream->common);
    if(in->resampler)
    {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }
    free(in->buffer);
    free(stream);
}
static int adev_dump(const audio_hw_device_t *device __unused, int fd __unused)
{
    return 0;
}
static int adev_close(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *)device;
    audio_route_free(adev->ar);
    mixer_close(adev->mixer);
    /* TFA9890 */
    enum Tfa98xx_Error err;
    err = Tfa98xx_Close(adev->tfa_handle);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_Close failed");
    }
    ALOGV("Tfa98xx closed");
    int i;
    for(i = 0; i < TFA_PATCH_MAX; i++)
    {
        free(adev->tfa_patch_data[i]->data);
        free(adev->tfa_patch_data[i]);
    }
    free(adev->tfa_config_data->data);
    free(adev->tfa_config_data);
    free(adev->tfa_speaker_data->data);
    free(adev->tfa_speaker_data);
    for(i = 0; i < TFA_TYPE_MAX; i++)
    {
        free(adev->tfa_preset_data[i]->data);
        free(adev->tfa_preset_data[i]);
        free(adev->tfa_eq_data[i]->data);
        free(adev->tfa_eq_data[i]);
    }
    return 0;
}
static int adev_open(const hw_module_t *module, const char *name, hw_device_t **device)
{
    struct audio_device *adev;
    int ret;
    if(strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
    {
        return -EINVAL;
    }
    adev = calloc(1, sizeof(struct audio_device));
    if(adev == NULL)
    {
        return -ENOMEM;
    }
    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;
    adev->ar = audio_route_init(MIXER_CARD, NULL);
    adev->input_source = AUDIO_SOURCE_DEFAULT;
    /* adev->cur_route_id initial value is 0 and such that first device
                     * selection is always applied by select_devices() */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->voice_volume = 1.0f;
    adev->mixer = mixer_open(PCM_CARD);
    /* TFA9890 */
    static const unsigned char TFA_SLAVE_ADDRESS = 0x68;
    enum Tfa98xx_Error err;
    err = Tfa98xx_Open(TFA_SLAVE_ADDRESS, &adev->tfa_handle);
    if(err != Tfa98xx_Error_Ok)
    {
        ALOGE("Tfa98xx_Open failed");
        return -EINVAL;
    }
    ALOGV("Tfa98xx opened");
    static const char *const TFA_PATCH_PATH[TFA_PATCH_MAX] = {
        "/system/etc/tfa98xx/TFA9890_N1C3_2_1_1.patch", "/system/etc/tfa98xx/coldboot.patch"};
    static const char *const TFA_CONFIG_PATH = "/system/etc/tfa98xx/TFA9890_N1B12_N1C3_v3.config";
    static const char *const TFA_SPEAKER_PATH = "/system/etc/tfa98xx/MZ_M76_speaker.speaker";
    static const char *const TFA_PRESET_PATH[TFA_TYPE_MAX] = {
        "/system/etc/tfa98xx/music_preset_0.preset",
        "/system/etc/tfa98xx/music_preset_1.preset",
        "/system/etc/tfa98xx/music_preset_2.preset",
        "/system/etc/tfa98xx/speech_preset.preset"};
    static const char *const TFA_EQ_PATH[TFA_TYPE_MAX] = {"/system/etc/tfa98xx/music_eq_0.eq",
                                                          "/system/etc/tfa98xx/music_eq_1.eq",
                                                          "/system/etc/tfa98xx/music_eq_2.eq",
                                                          "/system/etc/tfa98xx/speech_eq.eq"};
    int i;
    for(i = 0; i < TFA_PATCH_MAX; i++)
    {
        struct tfa_param_data *data = malloc(sizeof(struct tfa_param_data));
        unsigned int size;
        unsigned char *buf = load_file(TFA_PATCH_PATH[i], &size);
        if(!buf)
        {
            ALOGE("Tfa98xx patch file not found: %s", TFA_PATCH_PATH[i]);
            return -EINVAL;
        }
        data->size = size;
        data->data = buf;
        data->type = i;
        adev->tfa_patch_data[i] = data;
    }
    {
        struct tfa_param_data *data = malloc(sizeof(struct tfa_param_data));
        unsigned int size;
        unsigned char *buf = load_file(TFA_CONFIG_PATH, &size);
        if(!buf)
        {
            ALOGE("Tfa98xx config file not found: %s", TFA_CONFIG_PATH);
            return -EINVAL;
        }
        data->size = size;
        data->data = buf;
        data->type = i;
        adev->tfa_config_data = data;
        data = malloc(sizeof(struct tfa_param_data));
        buf = load_file(TFA_SPEAKER_PATH, &size);
        if(!buf)
        {
            ALOGE("Tfa98xx speaker file not found: %s", TFA_SPEAKER_PATH);
            return -EINVAL;
        }
        data->size = size;
        data->data = buf;
        data->type = i;
        adev->tfa_speaker_data = data;
    }
    for(i = 0; i < TFA_TYPE_MAX; i++)
    {
        struct tfa_param_data *data = malloc(sizeof(struct tfa_param_data));
        unsigned int size;
        unsigned char *buf = load_file(TFA_PRESET_PATH[i], &size);
        if(!buf)
        {
            ALOGE("Tfa98xx preset file not found: %s", TFA_PRESET_PATH[i]);
            return -EINVAL;
        }
        data->size = size;
        data->data = buf;
        data->type = i;
        adev->tfa_preset_data[i] = data;
        data = malloc(sizeof(struct tfa_param_data));
        buf = load_file(TFA_EQ_PATH[i], &size);
        if(!buf)
        {
            ALOGE("Tfa98xx EQ file not found: %s", TFA_EQ_PATH[i]);
            return -EINVAL;
        }
        data->size = size;
        data->data = buf;
        data->type = i;
        adev->tfa_eq_data[i] = data;
    }
    *device = &adev->hw_device.common;
    return 0;
}
static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};
struct audio_module HAL_MODULE_INFO_SYM = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = AUDIO_HARDWARE_MODULE_ID,
            .name = "MX4Pro audio module",
            .author = "Tatsuyuki Ishi",
            .methods = &hal_module_methods,
        },
};
