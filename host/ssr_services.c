/* (butterfly) Calls Nintendo Switch Sports Resort makes that Ocarina of Time did not, whose SDK code would open a service
   over IPC (the runtime answers those empty, and the SDK then aborts or reads through a null object): the console's one
   user (nn_applet.c's), and the network, which is never there (the game stays offline). */
#include "hos.h"
#include <windows.h>
#include <string.h>

/* (the user: nn_applet.c's user_uid, hos.h) */

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
/* ShowUserCreator() -> Result: the system's "add a user" screen (the game's "add character" button opens it). There is no
   such screen here, so it is cancelled: the game then counts the users (still one) and stays where it was. Answered 0
   (success, as an unimplemented import is), the game took it that a user was added, looked for them and aborted. */
#define R_ACCOUNT_CANCELLED 0x67C   /* (an account error: module 124, cancelled by the user) */
HLE(_ZN2nn7account15ShowUserCreatorEv) {
    hos_log("account: ShowUserCreator (no add-a-user screen here: cancelled)");
    RET(R_ACCOUNT_CANCELLED);
}
/* LoadProfileImage(unsigned long* size, void*, unsigned long, Uid const&) -> Result: the user has no picture */
#define R_ACCOUNT_NO_IMAGE 0x3A67C   /* (an account error: module 124) */
HLE(_ZN2nn7account16LoadProfileImageEPmPvmRKNS0_3UidE) {
    if (hos_guest_range(ARG(0), 8)) G64(ARG(0)) = 0;
    RET(R_ACCOUNT_NO_IMAGE);
}

/* ---- nn::pl: the console's built-in fonts (Switch 2's GetBuiltInFontData) ------------------------------------------- */
/* Plain TrueType data the game parses itself: Eden's open-source stand-ins for the firmware's fonts (ssr_fonts_data.c),
   in nn::pl's type order (0 standard, 1 Chinese simplified, 2 its extension, 3 Chinese traditional, 4 Korean,
   5 Nintendo extended). Copied once into guest memory, page-aligned. */
extern const unsigned char *const ssr_builtin_fonts[6];
extern const size_t ssr_builtin_font_sizes[6];
static uint64_t font_addr[6];
static SRWLOCK fonts_lock = SRWLOCK_INIT;
static void copy_fonts(void) {
    uint64_t total = 0;
    for (int i = 0; i < 6; i++) total += (ssr_builtin_font_sizes[i] + 0xFFF) & ~0xFFFull;
    const uint64_t base = hos_sys_alloc(total, 0x1000);
    uint64_t at = base;
    for (int i = 0; i < 6; i++) {
        memcpy(GP(at), ssr_builtin_fonts[i], ssr_builtin_font_sizes[i]);
        HOS_WRITTEN(GP(at), ssr_builtin_font_sizes[i]);
        font_addr[i] = at;
        at += (ssr_builtin_font_sizes[i] + 0xFFF) & ~0xFFFull;
    }
    hos_log("pl: the built-in fonts (%llu KB, open-source stand-ins) at %#llx", (unsigned long long)(total >> 10),
            (unsigned long long)base);
}
/* GetBuiltInFontData(BuiltInFontData{Nx,Ounce}* out, type, bool) -> bool (whether the font is there).
   out: {u8 flags, u8 flag bit, .., u64 address at +8, u32 size at +0x10} (the SDK's own fills it so) */
static void builtin_font(Ctx *c) {
    AcquireSRWLockExclusive(&fonts_lock);
    if (!font_addr[0]) copy_fonts();
    ReleaseSRWLockExclusive(&fonts_lock);
    const uint32_t type = (uint32_t)ARG(1);
    if (type >= 6 || !hos_guest_range(ARG(0), 0x18)) { RET(0); return; }
    *(uint8_t *)GP(ARG(0)) = 0;
    *(uint8_t *)GP(ARG(0) + 1) = 0;
    G64(ARG(0) + 8) = font_addr[type];
    G32(ARG(0) + 0x10) = (uint32_t)ssr_builtin_font_sizes[type];
    RET(1);
}
HLE(_ZN2nn2pl18GetBuiltInFontDataEPNS0_20BuiltInFontDataOunceENS0_20BuiltInFontTypeOunceEb) { builtin_font(c); }
HLE(_ZN2nn2pl18GetBuiltInFontDataEPNS0_17BuiltInFontDataNxENS0_17BuiltInFontTypeNxEb) { builtin_font(c); }
/* RequestLoadNxFont(void*, unsigned long): the fonts are always there */
HLE(_ZN2nn2pl17RequestLoadNxFontEPvm) { RET(0); }

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
