#include "music.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>

static MCIDEVICEID device_id;

void music_stop(void) {
    if (!device_id) return;
    mciSendCommandW(device_id, MCI_STOP, 0, 0);
    mciSendCommandW(device_id, MCI_CLOSE, 0, 0);
    device_id = 0;
}

bool music_start(const char *path) {
    music_stop();
    if (!path) return false;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (length <= 0) return false;
    wchar_t *wide_path = malloc((size_t)length * sizeof(*wide_path));
    if (!wide_path) return false;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, length)) {
        free(wide_path);
        return false;
    }

    MCI_OPEN_PARMSW open = {0};
    open.lpstrDeviceType = L"sequencer";
    open.lpstrElementName = wide_path;
    MCIERROR error = mciSendCommandW(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
                                     (DWORD_PTR)&open);
    free(wide_path);
    if (error) return false;
    device_id = open.wDeviceID;
    if (mciSendCommandW(device_id, MCI_PLAY, 0, 0)) {
        music_stop();
        return false;
    }
    return true;
}

void music_update(void) {
    if (!device_id) return;
    MCI_STATUS_PARMS status = {0};
    status.dwItem = MCI_STATUS_MODE;
    if (mciSendCommandW(device_id, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT,
                        (DWORD_PTR)&status)) {
        music_stop();
        return;
    }
    if (status.dwReturn != MCI_MODE_STOP) return;
    MCI_SEEK_PARMS seek = {0};
    if (mciSendCommandW(device_id, MCI_SEEK, MCI_SEEK_TO_START, (DWORD_PTR)&seek) ||
        mciSendCommandW(device_id, MCI_PLAY, 0, 0)) music_stop();
}
#else
bool music_start(const char *path) {
    (void)path;
    return false;
}

void music_update(void) {}
void music_stop(void) {}
#endif
