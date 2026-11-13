/* (butterfly) Joy-Cons and Pro Controllers as Lichii has them: one held in pairing mode (its sync button pressed) is
   paired with Windows by the game itself, no Bluetooth settings needed; and each is turned off when the game closes, so
   a button press wakes it and it reconnects next time.

   Pairing (Windows only): every 5 s while no Switch controller is open at all (an inquiry disturbs connected ones), a
   Bluetooth inquiry looks for a Joy-Con or Pro Controller that answers it and is not connected, and pairs it: bonded
   (Windows only makes a one-way link to a controller that reconnects by itself without the stored key), the PIN 0000
   for one that asks, the HID service on. A controller Windows remembers but can't reconnect is paired from the start
   again; one that isn't in pairing mode after all fails, and its half-made pairing is removed. Ported from Lichii's
   src/input_common/helpers/joycon_bluetooth_win.cpp (GPL-3.0-or-later). The Bluetooth calls are loaded at run time
   (BluetoothApis.dll, else bthprops.cpl): no link change, and nothing on a PC without Bluetooth.
   Turning off: HCI state 0 (subcommand 0x06, "disconnect and sleep"), sent through SDL's Switch driver as a raw effect
   (its first byte the subcommand). HOS_JOYCON_AUTO_PAIR=0 and HOS_JOYCON_POWER_OFF=0 (the environment or
   longshot_env.txt) turn either off. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int env_off(const char *name) {
    const char *e = getenv(name);
    return e && e[0] == '0';
}

/* ---- turning off -------------------------------------------------------------------------------------------- */
int hos_sdl_switch_power_off(void);   /* pad_sdl.c: how many were sent it */
void hos_joycon_power_off(void) {
    static volatile LONG done;
    if (InterlockedExchange(&done, 1) || env_off("HOS_JOYCON_POWER_OFF")) return;
    const int n = hos_sdl_switch_power_off();
    if (n) {
        hos_log("hid: %d Switch controller%s turned off (HOS_JOYCON_POWER_OFF=0 leaves them on)", n, n == 1 ? "" : "s");
        hos_log_flush();
        Sleep(150);   /* (the writes out before the process goes) */
    }
}

#ifdef _WIN32
#include <bluetoothapis.h>
#ifndef BLUETOOTH_SERVICE_ENABLE
#define BLUETOOTH_SERVICE_ENABLE 0x01   /* (not in every MinGW's bluetoothapis.h) */
#endif

/* ---- the Bluetooth calls, loaded at run time ----------------------------------------------------------------- */
static struct {
    HBLUETOOTH_RADIO_FIND (WINAPI *FindFirstRadio)(const BLUETOOTH_FIND_RADIO_PARAMS *, HANDLE *);
    BOOL (WINAPI *FindNextRadio)(HBLUETOOTH_RADIO_FIND, HANDLE *);
    BOOL (WINAPI *FindRadioClose)(HBLUETOOTH_RADIO_FIND);
    HBLUETOOTH_DEVICE_FIND (WINAPI *FindFirstDevice)(const BLUETOOTH_DEVICE_SEARCH_PARAMS *, BLUETOOTH_DEVICE_INFO *);
    BOOL (WINAPI *FindNextDevice)(HBLUETOOTH_DEVICE_FIND, BLUETOOTH_DEVICE_INFO *);
    BOOL (WINAPI *FindDeviceClose)(HBLUETOOTH_DEVICE_FIND);
    DWORD (WINAPI *RemoveDevice)(const BLUETOOTH_ADDRESS *);
    DWORD (WINAPI *RegisterForAuthenticationEx)(const BLUETOOTH_DEVICE_INFO *, HBLUETOOTH_AUTHENTICATION_REGISTRATION *,
                                                PFN_AUTHENTICATION_CALLBACK_EX, PVOID);
    BOOL (WINAPI *UnregisterAuthentication)(HBLUETOOTH_AUTHENTICATION_REGISTRATION);
    DWORD (WINAPI *AuthenticateDeviceEx)(HWND, HANDLE, BLUETOOTH_DEVICE_INFO *, PBLUETOOTH_OOB_DATA_INFO,
                                         BLUETOOTH_AUTHENTICATION_REQUIREMENTS);
    DWORD (WINAPI *SendAuthenticationResponseEx)(HANDLE, PBLUETOOTH_AUTHENTICATE_RESPONSE);
    DWORD (WINAPI *SetServiceState)(HANDLE, const BLUETOOTH_DEVICE_INFO *, const GUID *, DWORD);
    DWORD (WINAPI *GetDeviceInfo)(HANDLE, BLUETOOTH_DEVICE_INFO *);
} bt;

static int bt_load(void) {
    HMODULE m = LoadLibraryExA("BluetoothApis.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m || !GetProcAddress(m, "BluetoothAuthenticateDeviceEx")) m = LoadLibraryExA("bthprops.cpl", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) return 0;
#define BT(field, name) *(FARPROC *)&bt.field = GetProcAddress(m, name); if (!bt.field) return 0
    BT(FindFirstRadio, "BluetoothFindFirstRadio");
    BT(FindNextRadio, "BluetoothFindNextRadio");
    BT(FindRadioClose, "BluetoothFindRadioClose");
    BT(FindFirstDevice, "BluetoothFindFirstDevice");
    BT(FindNextDevice, "BluetoothFindNextDevice");
    BT(FindDeviceClose, "BluetoothFindDeviceClose");
    BT(RemoveDevice, "BluetoothRemoveDevice");
    BT(RegisterForAuthenticationEx, "BluetoothRegisterForAuthenticationEx");
    BT(UnregisterAuthentication, "BluetoothUnregisterAuthentication");
    BT(AuthenticateDeviceEx, "BluetoothAuthenticateDeviceEx");
    BT(SendAuthenticationResponseEx, "BluetoothSendAuthenticationResponseEx");
    BT(SetServiceState, "BluetoothSetServiceState");
    BT(GetDeviceInfo, "BluetoothGetDeviceInfo");
#undef BT
    return 1;
}

static int is_switch_controller(const BLUETOOTH_DEVICE_INFO *info) {
    return wcsncmp(info->szName, L"Joy-Con", 7) == 0 || wcscmp(info->szName, L"Pro Controller") == 0;
}

/* The inquiry also returns remembered controllers out of range, and one connected until a moment ago has a recent
   sighting: only one that answered this inquiry counts */
static int seen_since(const BLUETOOTH_DEVICE_INFO *info, const SYSTEMTIME *since) {
    FILETIME seen_ft, since_ft;
    if (!SystemTimeToFileTime(&info->stLastSeen, &seen_ft) || !SystemTimeToFileTime(since, &since_ft)) return 0;
    ULARGE_INTEGER seen, start;
    seen.LowPart = seen_ft.dwLowDateTime, seen.HighPart = seen_ft.dwHighDateTime;
    start.LowPart = since_ft.dwLowDateTime, start.HighPart = since_ft.dwHighDateTime;
    return seen.QuadPart + 10000000ULL >= start.QuadPart;   /* (1 s of slack, in 100 ns) */
}

static void name_of(const BLUETOOTH_DEVICE_INFO *info, char *out, int cap) {
    if (!WideCharToMultiByte(CP_UTF8, 0, info->szName, -1, out, cap, NULL, NULL)) snprintf(out, (size_t)cap, "?");
}

static BOOL CALLBACK accept_authentication(LPVOID ctx, PBLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS params) {
    (void)ctx;
    BLUETOOTH_AUTHENTICATE_RESPONSE response;
    memset(&response, 0, sizeof response);
    response.bthAddressRemote = params->deviceInfo.Address;
    response.authMethod = params->authenticationMethod;
    response.negativeResponse = FALSE;
    switch (params->authenticationMethod) {
    case BLUETOOTH_AUTHENTICATION_METHOD_NUMERIC_COMPARISON:
    case BLUETOOTH_AUTHENTICATION_METHOD_PASSKEY_NOTIFICATION:
        response.numericCompInfo.NumericValue = params->Numeric_Value;
        break;
    case BLUETOOTH_AUTHENTICATION_METHOD_LEGACY:
        response.pinInfo.pinLength = 4;
        memset(response.pinInfo.pin, '0', 4);
        break;
    default:
        break;
    }
    const DWORD r = bt.SendAuthenticationResponseEx(NULL, &response);
    hos_log("hid: pairing: authentication (method %d) answered, result %lu", (int)params->authenticationMethod,
            (unsigned long)r);
    return TRUE;
}

static void pair(HANDLE radio, BLUETOOTH_DEVICE_INFO *info) {
    char name[64];
    name_of(info, name, sizeof name);
    if (info->fRemembered) {
        /* (Windows can't use a controller that reconnects by itself: start over as with a new one) */
        const DWORD removed = bt.RemoveDevice(&info->Address);
        hos_log("hid: pairing: \"%s\": its old pairing removed (result %lu)", name, (unsigned long)removed);
        info->fRemembered = FALSE, info->fAuthenticated = FALSE;
    }
    HBLUETOOTH_AUTHENTICATION_REGISTRATION reg = NULL;
    bt.RegisterForAuthenticationEx(info, &reg, accept_authentication, NULL);
    const DWORD auth = bt.AuthenticateDeviceEx(NULL, radio, info, NULL, MITMProtectionNotRequiredBonding);
    if (reg) bt.UnregisterAuthentication(reg);
    if (auth != ERROR_SUCCESS) {
        bt.RemoveDevice(&info->Address);   /* (not in pairing mode after all: no half-made pairing left) */
        hos_log("hid: pairing: \"%s\" didn't pair (result %lu)", name, (unsigned long)auth);
        return;
    }
    GUID hid = {0x00001124, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};   /* (the HID service) */
    const DWORD on = bt.SetServiceState(radio, info, &hid, BLUETOOTH_SERVICE_ENABLE);
    bt.GetDeviceInfo(radio, info);
    hos_log("hid: pairing: \"%s\" paired (HID service %lu, bonded %d): it connects in a moment", name, (unsigned long)on,
            info->fAuthenticated != 0);
}

/* one inquiry (about 2.6 s): each Switch controller that answered it and is not connected, paired */
static void pair_discoverable(void) {
    BLUETOOTH_FIND_RADIO_PARAMS rp = {sizeof rp};
    HANDLE radio = NULL;
    HBLUETOOTH_RADIO_FIND rf = bt.FindFirstRadio(&rp, &radio);
    if (!rf) return;
    do {
        BLUETOOTH_DEVICE_SEARCH_PARAMS search;
        memset(&search, 0, sizeof search);
        search.dwSize = sizeof search;
        search.fReturnAuthenticated = TRUE;
        search.fReturnRemembered = TRUE;
        search.fReturnUnknown = TRUE;
        search.fReturnConnected = FALSE;
        search.fIssueInquiry = TRUE;
        search.cTimeoutMultiplier = 2;   /* (2.56 s) */
        search.hRadio = radio;
        SYSTEMTIME start;
        GetSystemTime(&start);
        BLUETOOTH_DEVICE_INFO info;
        memset(&info, 0, sizeof info);
        info.dwSize = sizeof info;
        HBLUETOOTH_DEVICE_FIND df = bt.FindFirstDevice(&search, &info);
        if (df) {
            do {
                if (is_switch_controller(&info) && !info.fConnected && seen_since(&info, &start)) pair(radio, &info);
                memset(&info, 0, sizeof info);
                info.dwSize = sizeof info;
            } while (bt.FindNextDevice(df, &info));
            bt.FindDeviceClose(df);
        }
        CloseHandle(radio);
    } while (bt.FindNextRadio(rf, &radio));
    bt.FindRadioClose(rf);
}

void hos_sdl_switch_pads(int *left, int *right, int *pro);   /* pad_sdl.c */
static DWORD WINAPI pair_main(LPVOID unused) {
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    Sleep(3000);   /* (controllers Windows reconnects by itself first) */
    for (;;) {
        int left = 0, right = 0, pro = 0;
        hos_sdl_switch_pads(&left, &right, &pro);
        /* (only while no Switch controller is open at all. An inquiry holds the PC's radio for 2.5 s: with one Joy-Con of a
           pair connected (Lichii's rule: until the set is whole) it lagged and dropped, the other one then too, and every
           drop meant more inquiries, every 5 s: a game where neither Joy-Con kept its connection. A bonded Joy-Con that
           drops comes back by itself; a second one in pairing mode waits until none is connected, or for Windows' own
           Bluetooth settings) */
        if (!pro && !left && !right) pair_discoverable();
        Sleep(5000);
    }
    return 0;
}

void hos_joycon_pair_start(void) {
    static volatile LONG started;
    if (InterlockedExchange(&started, 1)) return;
    if (env_off("HOS_JOYCON_AUTO_PAIR")) {
        hos_log("hid: pairing Switch controllers in pairing mode: off (HOS_JOYCON_AUTO_PAIR=0)");
        return;
    }
    if (!bt_load()) {
        hos_log("hid: pairing Switch controllers in pairing mode: no Bluetooth on this PC");
        return;
    }
    hos_log("hid: pairing Switch controllers in pairing mode: on (hold a Joy-Con's sync button to connect it)");
    CreateThread(NULL, 0, pair_main, NULL, 0, NULL);
}
#else
void hos_joycon_pair_start(void) {}
#endif
