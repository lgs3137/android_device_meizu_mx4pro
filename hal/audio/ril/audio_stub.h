#ifndef AUDIO_STUB_H_
#define AUDIO_STUB_H_
#include <linux/ioctl.h>
struct atc_header
{
    unsigned char cmd_code;
    unsigned char sub_cmd;
    unsigned char cmd_type;
    unsigned char data_len;
} __packed;
struct handshake_msg
{
    unsigned char pcm_master; /* 0:codec is master, 1:cp is master */
    unsigned char is_wb;      /* 0:nb, 1:wb */
    unsigned short reserved;
    unsigned int ver;
    unsigned int msg_id;
} __packed;
struct volume_ctlmsg
{
    unsigned char direction; /* 0:input, 1:output */
    unsigned char reserved[3];
    unsigned char gain;
    unsigned char hd_gain;
    unsigned char reserved2[2];
    unsigned int misc_volume;
    unsigned int msg_id;
} __packed;
struct mute_ctlmsg
{
    unsigned char direction; /* 0:input, 1:output */
    unsigned char mute;      /* 0:off, 1:on */
    unsigned short reserved;
    unsigned int msg_id;
} __packed;
struct path_ctlmsg
{
    unsigned int path;
    unsigned int msg_id;
} __packed;
struct eq_ctlmsg
{
    unsigned short reserved;
    unsigned short dha_mode;
    unsigned int dha_ch_flag;
    unsigned char dha_hearing_level[24];
    unsigned int msg_id;
} __packed;
struct loop_ctlmsg
{
    unsigned char test_mode; /* 0:off, 1:loopback pcm, 2:loopback packet */
    unsigned char reserved[3];
    unsigned int path;
    unsigned int msg_id;
} __packed;
struct pcm_record_ctlmsg
{
    unsigned char on_off;             /* 0:off, 1:on */
    unsigned char near_far_end;       /*1:near, 2:far, 3:both */
    unsigned char near_codec_vocoder; /* 1:near codec, 2:near vocoder */
    unsigned char reserved;
    /* callback field not used by AP, however it must be
    * different for playback and capture(record) stream
    * since CP uses this field as stream identifier.
    */
    unsigned int callback;
    unsigned int msg_id;
} __packed;
struct pcm_playback_ctlmsg
{
    unsigned char on_off;             /* 0:off, 1:on */
    unsigned char near_far_end;       /* 1:near, 2:far, 3:both */
    unsigned char near_codec_vocoder; /* 1:near codec, 2:near vocoder */
    unsigned char comb_with_call;     /* 0:not combined, 1:combined */
                                      /* callback field not used by AP, however it must be
                                      * different for playback and capture(record) stream
                                      * since CP uses this field as stream identifier.
                                      */
    unsigned int callback;
    unsigned int msg_id;
} __packed;
struct response_msg
{
    unsigned int status;
    unsigned int msg_id;
} __packed;
struct pcm_stream_ind
{
    unsigned int callback;
    unsigned int msg_id;
} __packed;
struct pcm_stream_data
{
    unsigned int callback;
    unsigned int msg_id;
    unsigned int len;
    unsigned char data[0];
} __packed;
#define AUDIODRV_MAGIC 'e'
#define AUDIOSTUB_GET_STATUS _IOR(AUDIODRV_MAGIC, 0x1, int)
#define AUDIOSTUB_GET_WRITECNT _IOR(AUDIODRV_MAGIC, 0x2, unsigned int)
#define AUDIOSTUB_GET_READCNT _IOR(AUDIODRV_MAGIC, 0x3, unsigned int)
#define AUDIOSTUB_SET_PKTSIZE _IOW(AUDIODRV_MAGIC, 0x4, unsigned int)
#define AUDIOSTUB_SET_CALLSTART _IOW(AUDIODRV_MAGIC, 0x5, int)
#define AUDIOSTUB_PCMPLAYBACK_DRAIN _IOW(AUDIODRV_MAGIC, 0x6, unsigned short)
#define AUDIOSTUB_VOLUMECTL _IOW(AUDIODRV_MAGIC, 0x10, struct volume_ctlmsg)
#define AUDIOSTUB_MUTECTL _IOW(AUDIODRV_MAGIC, 0x11, struct mute_ctlmsg)
#define AUDIOSTUB_PATHCTL _IOW(AUDIODRV_MAGIC, 0x12, struct path_ctlmsg)
#define AUDIOSTUB_EQCTL _IOW(AUDIODRV_MAGIC, 0x13, struct eq_ctlmsg)
#define AUDIOSTUB_LOOPBACKCTL _IOW(AUDIODRV_MAGIC, 0x14, struct loop_ctlmsg)
#define AUDIOSTUB_PCMRECCTL _IOW(AUDIODRV_MAGIC, 0x15, struct pcm_record_ctlmsg)
#define AUDIOSTUB_PCMPLAYBACKCTL _IOW(AUDIODRV_MAGIC, 0x16, struct pcm_playback_ctlmsg)
#endif /* AUDIO_STUB_H_ */
