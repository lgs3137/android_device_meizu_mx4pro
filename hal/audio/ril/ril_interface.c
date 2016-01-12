#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include "audio_stub.h"
#include "ril_interface.h"
#define CTL_PATH "/dev/audiostub_ctl"
int ril_open(int *handle)
{
    int fd;
    fd = open(CTL_PATH, O_RDONLY);
    if(!fd)
        return errno;
    *handle = fd;
    return 0;
}
int ril_close(int handle)
{
    if(close(handle) != 0)
    {
        return errno;
    }
    return 0;
}
int ril_setVolume(int handle, float volume)
{
    struct volume_ctlmsg msg = {0};
    msg.direction = 1;
    msg.hd_gain = msg.gain = 232 + lrintf(volume * 15);
    if(ioctl(handle, AUDIOSTUB_VOLUMECTL, &msg) == -1)
        return errno;
    return 0;
}
int ril_setMute(int handle, int mute)
{
    struct mute_ctlmsg msg = {0};
    msg.direction = 0;
    msg.mute = (mute == 1);
    if(ioctl(handle, AUDIOSTUB_MUTECTL, &msg) == -1)
        return errno;
    return 0;
}
int ril_setPath(int handle, enum ril_path path)
{
    struct path_ctlmsg msg = {0};
    msg.path = path;
    if(ioctl(handle, AUDIOSTUB_PATHCTL, &msg) == -1)
        return errno;
    struct volume_ctlmsg volume_msg = {0};
    volume_msg.direction = 1;
    volume_msg.hd_gain = volume_msg.gain = 3;
    if(ioctl(handle, AUDIOSTUB_VOLUMECTL, &volume_msg) == -1)
        return errno;
    return 0;
}
