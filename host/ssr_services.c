/* (butterfly) Calls Nintendo Switch Sports Resort makes that Ocarina of Time did not, whose SDK code would open a service
   over IPC (the runtime answers those empty, and the SDK then aborts or reads through a null object): the console's one
   user (nn_applet.c's), and the network, which is never there (the game stays offline). */
#include "hos.h"
#include <string.h>

/* nn_applet.c's user */
static const uint64_t user_uid[2] = {0x0000000000000001ULL, 0x5a3c9e1f0b7d2468ULL};

/* ---- nn::account ------------------------------------------------------------------------------------------------ */
/* GetUserCount(int*) -> Result */
HLE(_ZN2nn7account12GetUserCountEPi) {
    if (hos_guest_range(ARG(0), 4)) G32(ARG(0)) = 1;
    RET(0);
}
/* ListQualifiedUsers(int* count, Uid* out, int max) -> Result: the one user */
HLE(_ZN2nn7account18ListQualifiedUsersEPiPNS0_3UidEi) {
    const int n = (int)ARG(2) > 0 ? 1 : 0;
    if (n && hos_guest_range(ARG(1), 16)) {
        G64(ARG(1)) = user_uid[0];
        G64(ARG(1) + 8) = user_uid[1];
    }
    if (hos_guest_range(ARG(0), 4)) G32(ARG(0)) = (uint32_t)n;
    RET(0);
}
/* LoadProfileImage(unsigned long* size, void*, unsigned long, Uid const&) -> Result: the user has no picture */
#define R_ACCOUNT_NO_IMAGE 0x3A67C   /* (an account error: module 124) */
HLE(_ZN2nn7account16LoadProfileImageEPmPvmRKNS0_3UidE) {
    if (hos_guest_range(ARG(0), 8)) G64(ARG(0)) = 0;
    RET(R_ACCOUNT_NO_IMAGE);
}

/* ---- nn::nifm: no network ------------------------------------------------------------------------------------------ */
#define R_NIFM_NO_NETWORK 0xD46E   /* (a nifm error: module 110, "network not available") */
HLE(_ZN2nn4nifm10InitializeEv) { RET(0); }
HLE(_ZN2nn4nifm18IsNetworkAvailableEv) { RET(0); }
HLE(_ZN2nn4nifm19SetLocalNetworkModeEb) {}
HLE(_ZN2nn4nifm20CancelNetworkRequestEv) {}
HLE(_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv) {}
/* HandleNetworkRequestErrorResult() -> Result: the request failed (no error screen to show) */
HLE(_ZN2nn4nifm31HandleNetworkRequestErrorResultEv) { RET(R_NIFM_NO_NETWORK); }
HLE(_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_) { RET(R_NIFM_NO_NETWORK); }
HLE(_ZN2nn4nifm22GetCurrentIpConfigInfoEPNS_6socket6InAddrES3_S3_S3_S3_) { RET(R_NIFM_NO_NETWORK); }
HLE(_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE) { RET(R_NIFM_NO_NETWORK); }
