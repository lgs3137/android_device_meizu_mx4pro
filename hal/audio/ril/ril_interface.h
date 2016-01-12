#ifndef RIL_INTERFACE_H
#define RIL_INTERFACE_H
#define ril_handle_t int
enum ril_path
{
    RIL_PATH_SPEAKER = 1,
};
int ril_open(ril_handle_t *handle);
int ril_close(ril_handle_t handle);
int ril_setVolume(ril_handle_t handle, float volume);
int ril_setMute(ril_handle_t handle, int mute);
int ril_setPath(ril_handle_t handle, enum ril_path path);
#endif /* RIL_INTERFACE_H */
