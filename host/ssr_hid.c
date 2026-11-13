/* (butterfly) The controller calls Nintendo Switch Sports Resort makes beyond Ocarina of Time's: Joy-Cons held one at a time
   (the styles it supports, single and dual assignment), and the Switch 2's sensor fusion, the orientation its sports read
   the Joy-Con's swing from. The SDK's versions of these are thin calls into the system, so the lift stubs them out (0):
   the sports' setup took IsSensorFusionEnabled's 0 for a failure and waited forever.

   The orientation is host/pad_sdl.c's motion (Eden's filter on the gyroscope and accelerometer of the pad with motion used
   last: a Joy-Con through SDL), as GetSixAxisSensorStates hands it out, turned into the SensorFusionOrientation the game reads
   (laid out from its own use of them, 0x8015a254..: 0x50 bytes, newest first):
     +0x00 i64 sampling number   +0x08 i64 (kept the same while enabled)   +0x10 i64 the time since the one before (ns)
     +0x18 AccelerationType (read with GetG: here three floats, in G)
     +0x24 AngularVelocityType (read with GetDps: here three floats, in degrees a second)
     +0x30 the orientation as a quaternion x, y, z, w      +0x40 u8 status (2 bits: bit 1 calibrated)
   GetRotationMatrix gives the quaternion back as the 3x3 matrix (rows), the six-axis state's direction. */
#include "hos.h"
#include "hos_motion.h"
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NPAD_NO1 0
#define STYLE_FULLKEY (1u << 0)
#define STYLE_HANDHELD (1u << 1)
#define STYLE_JOYDUAL (1u << 2)
#define STYLE_JOYLEFT (1u << 3)
#define STYLE_JOYRIGHT (1u << 4)

/* ---- the npad: its styles and its Joy-Cons' assignment ----------------------------------------------------------- */
static volatile uint32_t supported_styles;   /* SetSupportedNpadStyleSet's (0: not set) */
static volatile int single_assigned;         /* No.1 set to single (one Joy-Con) */

/* The style No.1 shows (nn_hid.c's GetNpadStyleSet): a full key controller while the game takes one, as Ocarina of Time
   does; else a right Joy-Con alone once the game set No.1 to single, or a Joy-Con pair */
uint32_t hos_npad_style(void) {
    const uint32_t s = supported_styles;
    if (!s || (s & STYLE_FULLKEY && !single_assigned)) return STYLE_FULLKEY;
    if (single_assigned && (s & STYLE_JOYRIGHT)) return STYLE_JOYRIGHT;
    if (s & STYLE_JOYDUAL) return STYLE_JOYDUAL;
    if (s & STYLE_JOYRIGHT) return STYLE_JOYRIGHT;
    if (s & STYLE_FULLKEY) return STYLE_FULLKEY;
    return s & (0u - s);   /* (the lowest the game takes) */
}
void hos_npad_set_supported(uint32_t styles) {
    if (styles != supported_styles)
        hos_log("hid: the game takes npad styles %#x: No.1 shows %#x", styles, (supported_styles = styles, hos_npad_style()));
}
uint32_t hos_npad_supported(void) { return supported_styles; }

HLE(_ZN2nn3hid24GetSupportedNpadStyleSetEv) { RET(supported_styles); }
/* GetNpadJoyAssignment(unsigned int const&) -> NpadJoyAssignmentMode: 0 dual, 1 single */
HLE(_ZN2nn3hid20GetNpadJoyAssignmentERKj) { RET(G32(ARG(0)) == NPAD_NO1 && single_assigned ? 1 : 0); }
static void set_single(uint32_t id, int single) {
    if (id != NPAD_NO1 || single_assigned == single) return;
    single_assigned = single;
    hos_log("hid: No.1 set to %s: it shows style %#x", single ? "single (one Joy-Con)" : "dual (a pair)", hos_npad_style());
}
HLE(_ZN2nn3hid30SetNpadJoyAssignmentModeSingleERKj) { set_single(G32(ARG(0)), 1); }
HLE(_ZN2nn3hid30SetNpadJoyAssignmentModeSingleERKjNS0_17NpadJoyDeviceTypeE) { set_single(G32(ARG(0)), 1); }
HLE(_ZN2nn3hid28SetNpadJoyAssignmentModeDualERKj) { set_single(G32(ARG(0)), 0); }
HLE(_ZN2nn3hid18SwapNpadAssignmentERKjS2_) { RET(0); }
HLE(_ZN2nn3hid23MergeSingleJoyAsDualJoyERKjS2_) { RET(0); }

/* ---- six-axis extras ------------------------------------------------------------------------------------------- */
/* IsSixAxisSensorAtRest(handle) -> bool: the motion pad lying still (pad_sdl.c's filter). Switch Sports' calibration
   asks for the Joy-Con laid on a table and waits for this */
HLE(_ZN2nn3hid21IsSixAxisSensorAtRestERKNS0_19SixAxisSensorHandleE) {
    int hos_sdl_motion_still(float stats[3]);
    float st[3];
    const int still = hos_sdl_motion_still(st);
    static int was = -1;
    if (still != was) {   /* (the change, with the sensors' noise then) */
        hos_log("hid: at rest: %s (gyro spread %.4f rad/s, accel spread %.4f G)", still ? "yes" : "no", st[0], st[1]);
        was = still;
    }
    RET(still);
}
/* GetGyroscopeZeroDriftMode(handle) -> GyroscopeZeroDriftMode: 1 standard (what the motion filter does) */
HLE(_ZN2nn3hid25GetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleE) { RET(1); }
HLE(_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE) {}

/* ---- the Joy-Cons' colours (butterfly) ---------------------------------------------------------------------------------
   NpadJoyIconDecorationColor, 12 bytes: three RGBA colours (the game reads bytes 0-2, 4-6 and 8-10 of each: 0x803c4dc0),
   body, buttons and the third. Unanswered, the game drew what its stack held: the left Joy-Con's icon in a mix of colours,
   flashing as they changed. A Joy-Con keeps its colours in its own memory, which SDL does not read out, so they come from
   HOS_JOYCON_COLOR_LEFT / HOS_JOYCON_COLOR_RIGHT (the environment or longshot_env.txt): "RRGGBB" the body, "RRGGBB,RRGGBB"
   the body and the buttons; else the grey Joy-Con's (body 828282, buttons 0F0F0F) */
static void joycon_colors(int right, uint8_t out[12]) {
    uint32_t body = 0x828282, buttons = 0x0F0F0F;
    const char *e = getenv(right ? "HOS_JOYCON_COLOR_RIGHT" : "HOS_JOYCON_COLOR_LEFT");
    if (e && *e) {
        char *end = NULL;
        const unsigned long b = strtoul(e[0] == '#' ? e + 1 : e, &end, 16);
        if (end && end != e) body = (uint32_t)b & 0xFFFFFF;
        if (end && *end == ',') {
            const char *t = end + 1;
            const unsigned long k = strtoul(t[0] == '#' ? t + 1 : t, &end, 16);
            if (end != t) buttons = (uint32_t)k & 0xFFFFFF;
        }
    }
    const uint32_t c[3] = {body, buttons, body};
    for (int i = 0; i < 3; i++) {
        out[4 * i] = (uint8_t)(c[i] >> 16), out[4 * i + 1] = (uint8_t)(c[i] >> 8), out[4 * i + 2] = (uint8_t)c[i];
        out[4 * i + 3] = 0xFF;
    }
}
static void put_colors(uint64_t at, int right) {
    if (!hos_guest_range(at, 12)) return;
    uint8_t b[12];
    joycon_colors(right, b);
    memcpy(GP(at), b, sizeof b);
    HOS_WRITTEN(GP(at), sizeof b);
}
/* GetNpadJoyIconDecorationColor(Color* left, Color* right, unsigned int const& npad): a pair's */
HLE(_ZN2nn3hid29GetNpadJoyIconDecorationColorEPNS0_26NpadJoyIconDecorationColorES2_RKj) {
    put_colors(ARG(0), 0);
    put_colors(ARG(1), 1);
    RET(0);
}
/* GetNpadJoyIconDecorationColor(Color*, unsigned int const& npad): one controller's (a lone left Joy-Con the left's) */
HLE(_ZN2nn3hid29GetNpadJoyIconDecorationColorEPNS0_26NpadJoyIconDecorationColorERKj) {
    put_colors(ARG(0), hos_npad_style() == STYLE_JOYLEFT ? 0 : 1);
    RET(0);
}

/* ---- sensor fusion ---------------------------------------------------------------------------------------------- */
#define FUSION_HANDLE 0x5346u   /* ("SF") */
#define ENTRY 0x50
/* The devices a handle names: FUSION_HANDLE + 0 the left Joy-Con, + 1 the right one, each with its own motion (pad_sdl.c
   hos_sdl_motion_history_dev: archery aims with the left and draws with the right), + 2 a controller of its own (a lone
   Joy-Con's style that is neither, a Pro Controller: the pad used last). Each its own on/off, yaw reset, orientation and
   sampling numbers; the game turns them on and off one at a time */
#define FUSION_DEVS 3
static const char *const dev_name[FUSION_DEVS] = {"left Joy-Con", "right Joy-Con", "controller"};
static volatile int fusion_on[FUSION_DEVS];
static int fusion_slot(uint32_t h) { return h >= FUSION_HANDLE && h < FUSION_HANDLE + FUSION_DEVS ? (int)(h - FUSION_HANDLE) : -1; }
static float yaw_fixes[FUSION_DEVS][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}};   /* on the world's side (ResetSensorFusionYaw) */
static SRWLOCK fusion_lock = SRWLOCK_INIT;
static int64_t fusion_base[FUSION_DEVS], fusion_last[FUSION_DEVS];
static int fusion_was_live[FUSION_DEVS], fusion_src[FUSION_DEVS] = {-1, -1, -1};
int hos_sdl_motion_history_dev(int dev, HosMotionSample *out, int max, int *src, int *calibrated);   /* pad_sdl.c */

HLE(_ZN2nn3hid25GetSensorFusionMemorySizeEv) { RET(0x1000); }
HLE(_ZN2nn3hid29GetSensorFusionWorkBufferSizeEv) { RET(0x1000); }
HLE(_ZN2nn3hid22InitializeSensorFusionEPvm) { RET(0); }
HLE(_ZN2nn3hid20FinalizeSensorFusionEv) {
    for (int k = 0; k < FUSION_DEVS; k++) fusion_on[k] = 0;
    hos_log("hid: sensor fusion finalized");
}
/* GetSensorFusionHandles(SensorFusionHandle*, int, unsigned int const& npad, BitFlagSet styles) -> int: No.1's, one per
   Joy-Con a pair has (left, right: the game picks min(count - 1, its Joy-Con's index)), else one: a lone Joy-Con's own,
   or the controller's */
HLE(_ZN2nn3hid22GetSensorFusionHandlesEPNS0_18SensorFusionHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE) {
    static int logged;
    if (logged++ < 4) hos_log("hid: sensor fusion handles asked for: npad %u, %d wanted, styles %#x", G32(ARG(2)), (int)ARG(1),
                              (uint32_t)ARG(3));
    if ((int)ARG(1) < 1 || G32(ARG(2)) != NPAD_NO1) { RET(0); return; }
    const int n = (int)ARG(1) >= 2 && ((uint32_t)ARG(3) & STYLE_JOYDUAL) ? 2 : 1;
    if (n == 2) {
        G32(ARG(0)) = FUSION_HANDLE, G32(ARG(0) + 4) = FUSION_HANDLE + 1;
    } else {
        const uint32_t style = hos_npad_style();
        G32(ARG(0)) = FUSION_HANDLE + (style == STYLE_JOYLEFT ? 0 : style == STYLE_JOYRIGHT ? 1 : 2);
    }
    RET(n);
}
HLE(_ZN2nn3hid18EnableSensorFusionENS0_18SensorFusionHandleEPvm) {
    const int k = fusion_slot((uint32_t)ARG(0));
    if (k < 0) { RET(0); return; }
    if (!fusion_on[k]) hos_log("hid: sensor fusion on for the %s (the orientation from the motion controls)", dev_name[k]);
    fusion_on[k] = 1;
    RET(0);
}
HLE(_ZN2nn3hid19DisableSensorFusionENS0_18SensorFusionHandleE) {
    const int k = fusion_slot((uint32_t)ARG(0));
    if (k >= 0 && fusion_on[k]) hos_log("hid: sensor fusion off for the %s", dev_name[k]);
    if (k >= 0) fusion_on[k] = 0;
    RET(0);
}
HLE(_ZN2nn3hid21IsSensorFusionEnabledENS0_18SensorFusionHandleE) {
    const int k = fusion_slot((uint32_t)ARG(0));
    RET(k >= 0 && fusion_on[k] ? 1 : 0);
}
HLE(_ZN2nn3hid18UpdateSensorFusionENS0_18SensorFusionHandleE) { RET(0); }

/* the quaternion (x, y, z, w) of a rotation matrix (rows), and back */
static void quat_of(const float *m, float q[4]) {
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        const float s = sqrtf(tr + 1.0f) * 2;
        q[3] = 0.25f * s, q[0] = (m[7] - m[5]) / s, q[1] = (m[2] - m[6]) / s, q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
        q[3] = (m[7] - m[5]) / s, q[0] = 0.25f * s, q[1] = (m[1] + m[3]) / s, q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        const float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
        q[3] = (m[2] - m[6]) / s, q[0] = (m[1] + m[3]) / s, q[1] = 0.25f * s, q[2] = (m[5] + m[7]) / s;
    } else {
        const float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
        q[3] = (m[3] - m[1]) / s, q[0] = (m[2] + m[6]) / s, q[1] = (m[5] + m[7]) / s, q[2] = 0.25f * s;
    }
    const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 0) for (int i = 0; i < 4; i++) q[i] /= n;
    else q[0] = q[1] = q[2] = 0, q[3] = 1;
}
static void matrix_of(const float q[4], float *m) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z), m[1] = 2 * (x * y - z * w), m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w), m[4] = 1 - 2 * (x * x + z * z), m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w), m[7] = 2 * (y * z + x * w), m[8] = 1 - 2 * (x * x + y * y);
}
static void quat_mul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
/* The orientation as the Switch 2's fusion has it, from the motion filter's direction matrix. The filter's matrix read
   the other way round (the world in the Joy-Con's axes) gave the game a mirror image: every turn backwards (the plane's
   pitch and yaw, the table tennis paddle). So by default the quaternion is the conjugate (the transposed matrix).
   BUTTERFLY_MOTION_FLIP tries others with no rebuild: "c" or unset the conjugate, "none" the matrix as it is, and an axis
   letter (x, y, z) adds a half turn about that axis of the Joy-Con, e.g. "cx", "y". */
static int flip_conj = 1, flip_axis = -1;
static void device_flip_read(void) {
    static volatile int done;
    if (done) return;
    const char *e = getenv("BUTTERFLY_MOTION_FLIP");
    if (e && *e) {
        flip_conj = 0;
        for (const char *p = e; *p; p++) {
            const char ch = (char)(*p | 0x20);
            if (ch == 'c') flip_conj = 1;
            else if (ch >= 'x' && ch <= 'z') flip_axis = ch - 'x';
        }
    }
    hos_log("hid: sensor fusion: the orientation %s%s%s (BUTTERFLY_MOTION_FLIP=%s)", flip_conj ? "conjugated" : "as the filter has it",
            flip_axis >= 0 ? ", half a turn about " : "", flip_axis == 0 ? "x" : flip_axis == 1 ? "y" : flip_axis == 2 ? "z" : "",
            e ? e : "unset");
    done = 1;
}
/* the motion's orientation in the fusion's Joy-Con axes (before the yaw reset) */
static void device_quat(const float *direction, float q[4]) {
    device_flip_read();
    float raw[4];
    quat_of(direction, raw);
    if (flip_conj) raw[0] = -raw[0], raw[1] = -raw[1], raw[2] = -raw[2];
    if (flip_axis < 0) { memcpy(q, raw, sizeof raw); return; }
    float f[4] = {0, 0, 0, 0};
    f[flip_axis] = 1;
    quat_mul(raw, f, q);
}
/* the orientation handed out: that, turned about the world's up (z) by the yaw reset */
static void fused(const HosMotionState *m, int dev, float q[4]) {
    float d[4];
    device_quat(m->direction, d);
    AcquireSRWLockShared(&fusion_lock);
    quat_mul(yaw_fixes[dev], d, q);
    ReleaseSRWLockShared(&fusion_lock);
}
/* The orientation integrated from the very angular velocity and acceleration handed out with it (+0x24, +0x18), so the
   three always agree: the game steered right from those before it took the Joy-Con for calibrated, and wrong from an
   orientation built another way. The Joy-Con's own rates turn it (q' = q (x) w dt, q the Joy-Con's turn into the world),
   and the accelerometer pulls its tilt toward gravity (Mahony, as the console's fusion corrects drift); lying flat (the
   accelerometer reading 0, 0, -1 G) is no turn, and the yaw is the game's to reset. BUTTERFLY_MOTION_FLIP set: the
   direction-matrix orientation instead (the earlier tries). */
static SRWLOCK integ_lock = SRWLOCK_INIT;
static float integ_qs[FUSION_DEVS][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}};   /* (each device's own) */
static float integ_rings[FUSION_DEVS][HOS_MOTION_HISTORY][4];
static int use_integration(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BUTTERFLY_MOTION_FLIP");
        v = !(e && *e);
        hos_log("hid: sensor fusion: %s", v ? "the orientation integrated from the rates handed out with it"
                                            : "the direction-matrix orientation (BUTTERFLY_MOTION_FLIP set)");
    }
    return v;
}
static void q_norm(float q[4]) {
    const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 0) for (int i = 0; i < 4; i++) q[i] /= n;
    else q[0] = q[1] = q[2] = 0, q[3] = 1;
}
/* the turn that takes (0, 0, flat) to the reading a (the tilt alone, no yaw); flat: the z reading of the Joy-Con lying
   flat, buttons up, in the fusion's axes (-1, or +1 with z turned around): that pose is no turn */
static void q_from_accel(const float *a, float flat, float q[4]) {
    const float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 1e-3f) { q[0] = q[1] = q[2] = 0, q[3] = 1; return; }
    /* the Joy-Con's turn R has R^T (0,0,flat) = a/|a|: q is the turn from a/|a| to (0, 0, flat) */
    const float u[3] = {a[0] / n, a[1] / n, a[2] / n}, v[3] = {0, 0, flat};
    const float d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    if (d < -0.9999f) { q[0] = 1, q[1] = q[2] = q[3] = 0; return; }
    q[0] = u[1] * v[2] - u[2] * v[1], q[1] = u[2] * v[0] - u[0] * v[2], q[2] = u[0] * v[1] - u[1] * v[0], q[3] = 1 + d;
    q_norm(q);
}
/* The fusion's Joy-Con axes against the six-axis state's (which Ocarina of Time and the game's own pre-calibration aim read
   right): each axis kept or turned around. The default turns all three around (7, with the orientation handed out the other way round, below: the turns of none
   turned, 1, with gravity read as the calibration accepts it); 6 turns z around: with the six-axis state's axes the calibrated aim had
   pitch and yaw backwards and the calibration's gravity check failed; with x and z turned around (0) all was right but the
   roll (the plane's bank), mirrored. BUTTERFLY_MOTION_AXES=0..7 (the environment) takes another. An axis turned
   around turns the acceleration's part on it; the angular velocity, an axial vector, turns with det(T) T. */
static const signed char axes_sign[8][3] = {{-1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {-1, -1, 1},
                                             {1, -1, -1}, {-1, 1, 1}, {1, 1, -1}, {-1, -1, -1}};
static volatile int axes_pick = -1;
static int64_t integ_ns[FUSION_DEVS] = {-1, -1, -1};   /* the newest reading integrated (its own count), each device */
/* The orientation handed out the other way round (world to Joy-Con, the conjugate): with all axes turned around (variant
   7) that gives the game its pitch, pointing and bank all the right way (with z turned around (6) the plane was right
   but table tennis's pointing mirrored; with x and z (0), the bank). BUTTERFLY_MOTION_CONJ=0 (the environment) hands it
   out Joy-Con to world. (Found with F7 and F8 trying them in the game, since taken out: a key pressed by chance turned
   the controls around.) */
static volatile int conj_out = -1;
static int motion_conj(void) {
    if (conj_out < 0) {
        const char *e = getenv("BUTTERFLY_MOTION_CONJ");
        conj_out = !(e && *e == '0');
        if (e && *e) hos_log("hid: sensor fusion orientation handed out %s (BUTTERFLY_MOTION_CONJ)", conj_out ? "world to Joy-Con" : "Joy-Con to world");
    }
    return conj_out;
}
static int motion_axes(void) {
    if (axes_pick < 0) {
        const char *e = getenv("BUTTERFLY_MOTION_AXES");
        axes_pick = e && *e >= '0' && *e <= '7' ? *e - '0' : 7;
        if (e && *e) hos_log("hid: sensor fusion axes: variant %d (BUTTERFLY_MOTION_AXES)", axes_pick);
    }
    return axes_pick;
}
/* a reading's acceleration (G) and angular velocity (turns a second) in the fusion's axes */
static void fusion_axes(const HosMotionState *m, int v, float a[3], float g[3]) {
    const signed char *t = axes_sign[v];
    const int det = t[0] * t[1] * t[2];
    for (int k = 0; k < 3; k++) a[k] = t[k] * m->accel[k], g[k] = (float)(det * t[k]) * m->gyro[k];
}
static void integrate_one(const HosMotionSample *s, int v, float *integ_q) {
    HosMotionState mm;
    fusion_axes(&s->st, v, mm.accel, mm.gyro);
    const HosMotionState *m = &mm;
    float dt = s->dt_ns > 0 ? (float)s->dt_ns * 1e-9f : 0.005f;
    if (dt > 0.1f) dt = 0.1f;
    float w[3];
    for (int k = 0; k < 3; k++) w[k] = m->gyro[k] * 6.28318531f;   /* (turns a second: radians a second) */
    const float an = sqrtf(m->accel[0] * m->accel[0] + m->accel[1] * m->accel[1] + m->accel[2] * m->accel[2]);
    if (an > 0.8f && an < 1.2f) {
        float r[9];
        matrix_of(integ_q, r);
        const float flat = -(float)axes_sign[v][2];
        const float ref[3] = {flat * r[6], flat * r[7], flat * r[8]};   /* the reading lying this way would give: R^T (0, 0, flat) */
        const float a[3] = {m->accel[0] / an, m->accel[1] / an, m->accel[2] / an};
        const float e[3] = {a[1] * ref[2] - a[2] * ref[1], a[2] * ref[0] - a[0] * ref[2], a[0] * ref[1] - a[1] * ref[0]};
        const float kp = 1.0f;
        for (int k = 0; k < 3; k++) w[k] += kp * e[k];
    }
    const float x = integ_q[0], y = integ_q[1], z = integ_q[2], q0 = integ_q[3];
    const float hd = 0.5f * dt;
    integ_q[0] += hd * (q0 * w[0] + y * w[2] - z * w[1]);
    integ_q[1] += hd * (q0 * w[1] - x * w[2] + z * w[0]);
    integ_q[2] += hd * (q0 * w[2] + x * w[1] - y * w[0]);
    integ_q[3] += hd * (-x * w[0] - y * w[1] - z * w[2]);
    q_norm(integ_q);
}
/* each reading of h (newest first) its integrated orientation, into qs */
static void integrated(const HosMotionSample *h, int live, int v, int dev, float qs[][4]) {
    AcquireSRWLockExclusive(&integ_lock);
    float *integ_q = integ_qs[dev];
    float (*integ_ring)[4] = integ_rings[dev];
    int64_t integ_n = integ_ns[dev];
    if (integ_n < 0 || h[0].n < integ_n) {
        float a0[3], g0[3];
        fusion_axes(&h[live - 1].st, v, a0, g0);
        q_from_accel(a0, -(float)axes_sign[v][2], integ_q);   /* (first, or a new motion pad: the tilt from gravity; a gap is
                                                          integrated over from what is left of it) */
        integ_n = h[live - 1].n;
        memcpy(integ_ring[integ_n % HOS_MOTION_HISTORY], integ_q, 16);
    }
    for (int i = live - 1; i >= 0; i--) {
        if (h[i].n <= integ_n) continue;
        integrate_one(&h[i], v, integ_q);
        integ_n = h[i].n;
        memcpy(integ_ring[integ_n % HOS_MOTION_HISTORY], integ_q, 16);
    }
    for (int i = 0; i < live; i++) {
        if (integ_n - h[i].n < HOS_MOTION_HISTORY) memcpy(qs[i], integ_ring[h[i].n % HOS_MOTION_HISTORY], 16);
        else memcpy(qs[i], integ_q, 16);
    }
    integ_ns[dev] = integ_n;
    ReleaseSRWLockExclusive(&integ_lock);
}
/* ResetSensorFusionYaw(handle): the way that device points now is straight ahead */
HLE(_ZN2nn3hid20ResetSensorFusionYawENS0_18SensorFusionHandleE) {
    const int dev = fusion_slot((uint32_t)ARG(0));
    HosMotionSample h[1];
    int src, cal;
    if (dev >= 0 && hos_sdl_motion_history_dev(dev, h, 1, &src, &cal) == 1) {
        float d[4], m[9];
        if (use_integration()) {
            AcquireSRWLockShared(&integ_lock);
            memcpy(d, integ_qs[dev], sizeof d);
            ReleaseSRWLockShared(&integ_lock);
        } else {
            device_quat(h[0].st.direction, d);
        }
        matrix_of(d, m);
        const float yaw = atan2f(m[3], m[0]);   /* (the Joy-Con's x axis in the world: column 0) */
        AcquireSRWLockExclusive(&fusion_lock);
        float *y = yaw_fixes[dev];
        y[0] = y[1] = 0, y[2] = sinf(-yaw / 2), y[3] = cosf(-yaw / 2);
        ReleaseSRWLockExclusive(&fusion_lock);
    }
    RET(0);
}
/* GetSensorFusionOrientations(SensorFusionOrientation* out, int max, SensorFusionHandle) -> int: newest first */
HLE(_ZN2nn3hid27GetSensorFusionOrientationsEPNS0_23SensorFusionOrientationEiNS0_18SensorFusionHandleE) {
    const uint64_t out = ARG(0);
    const int max = (int)ARG(1) < 1 ? 0 : (int)ARG(1) > HOS_MOTION_HISTORY ? HOS_MOTION_HISTORY : (int)ARG(1);
    if (!max || !hos_guest_range(out, (uint64_t)max * ENTRY)) { RET(0); return; }
    const int k = fusion_slot((uint32_t)ARG(2));
    const int dev = k >= 0 ? k : 2;
    HosMotionSample h[HOS_MOTION_HISTORY];
    int src = 2, calibrated = 0;
    const int live = hos_sdl_motion_history_dev(dev, h, max, &src, &calibrated);
    /* (another source than last time, the left Joy-Con's own after the pad used last, say: its own reading numbers, so
       its orientation starts afresh from gravity and its sampling numbers go on past the last ones) */
    if (live && src != fusion_src[dev]) {
        fusion_was_live[dev] = 0;
        AcquireSRWLockExclusive(&integ_lock);
        integ_ns[dev] = -1;
        ReleaseSRWLockExclusive(&integ_lock);
        fusion_src[dev] = src;
    }
    /* (sampling numbers as GetSixAxisSensorStates hands them out: always on, a whole history apart between live and rest) */
    int64_t *base = &fusion_base[dev], *last = &fusion_last[dev];
    if (live) {
        if (!fusion_was_live[dev] || *base + h[0].n < *last) *base = *last + HOS_MOTION_HISTORY - h[0].n;
        *last = *base + h[0].n;
    } else {
        *last += fusion_was_live[dev] ? HOS_MOTION_HISTORY : 1;
    }
    fusion_was_live[dev] = live > 0;
    /* status (2 bits): bit 1 the sensors calibrated (the game's calibration screen waits for it, 0x80c7bd00) */
    const int status = live && calibrated ? 2 : 0;
    static const HosMotionState at_rest = {{0, 0, -1}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    const int n = live ? live : 1;
    static float qs[HOS_MOTION_HISTORY][4];   /* (under the history's own order: one caller at a time below) */
    static SRWLOCK out_lock = SRWLOCK_INIT;
    const int integ = use_integration() && live;
    AcquireSRWLockExclusive(&out_lock);
    const int axv = motion_axes(), cj = motion_conj();
    if (integ) integrated(h, live, axv, dev, qs);
    for (int i = 0; i < n; i++) {
        const uint64_t e = out + (uint64_t)i * ENTRY;
        const HosMotionState *m = live ? &h[i].st : &at_rest;
        float q[4], dps[3];
        if (integ) {
            AcquireSRWLockShared(&fusion_lock);
            quat_mul(yaw_fixes[dev], qs[i], q);
            ReleaseSRWLockShared(&fusion_lock);
        } else {
            fused(m, dev, q);
        }
        float acc[3], rev[3];
        if (integ) fusion_axes(m, axv, acc, rev);
        else memcpy(acc, m->accel, 12), memcpy(rev, m->gyro, 12);
        if (integ && cj) {
            /* handed out the other way round (R' = R^T, R the Joy-Con's turn into the world): the rates and acceleration in
               the axes that R' implies, so all three agree as they did (a swing's force is read from them along the way the
               Joy-Con faces: in the old axes a forehand came out weak). R' turning at w' needs w' = -R w; gravity seen in
               R''s axes is R'^T g = R g, so a reading a = R^T g + linear part becomes R (R a). */
            float r[9], t[3];
            matrix_of(q, r);
            for (int k = 0; k < 3; k++) t[k] = r[3 * k] * rev[0] + r[3 * k + 1] * rev[1] + r[3 * k + 2] * rev[2];
            for (int k = 0; k < 3; k++) rev[k] = -t[k];
            for (int pass = 0; pass < 2; pass++) {
                for (int k = 0; k < 3; k++) t[k] = r[3 * k] * acc[0] + r[3 * k + 1] * acc[1] + r[3 * k + 2] * acc[2];
                memcpy(acc, t, sizeof t);
            }
            q[0] = -q[0], q[1] = -q[1], q[2] = -q[2];
        }
        for (int k = 0; k < 3; k++) dps[k] = rev[k] * 360.0f;
        memset(GP(e), 0, ENTRY);
        G64(e) = (uint64_t)(live ? *base + h[i].n : *last - i);
        G64(e + 8) = 1;
        G64(e + 0x10) = (uint64_t)(live && h[i].dt_ns > 0 ? h[i].dt_ns : 5000000);
        memcpy(GP(e + 0x18), acc, 12);
        memcpy(GP(e + 0x24), dps, 12);
        memcpy(GP(e + 0x30), q, 16);
        *(uint8_t *)GP(e + 0x40) = (uint8_t)status;
    }
    ReleaseSRWLockExclusive(&out_lock);
    HOS_WRITTEN(GP(out), (size_t)n * ENTRY);
    static int logged;
    if (!(logged & 1 << dev)) {
        logged |= 1 << dev;
        hos_log("hid: sensor fusion: the %s's first orientations (%s)", dev_name[dev],
                !live ? "no pad with motion: at rest" : src == 2 ? "the pad used last" : src ? "the right Joy-Con's own" : "the left Joy-Con's own");
    }
    RET(n);
}
/* the three float returns of GetG / GetDps (s0, s1, s2) */
static void ret_floats3(Ctx *c, const float *f) {
    for (int i = 0; i < 3; i++) {
        uint32_t bits;
        memcpy(&bits, &f[i], 4);
        c->vreg[i][0] = bits, c->vreg[i][1] = 0;
    }
}
/* AccelerationType::GetG() const -> {x, y, z} G; AngularVelocityType::GetDps() const -> degrees a second */
HLE(_ZNK2nn3hid16AccelerationType4GetGEv) {
    float f[3] = {0, 0, 0};
    if (hos_guest_range(ARG(0), 12)) memcpy(f, GP(ARG(0)), 12);
    ret_floats3(c, f);
}
HLE(_ZNK2nn3hid19AngularVelocityType6GetDpsEv) {
    float f[3] = {0, 0, 0};
    if (hos_guest_range(ARG(0), 12)) memcpy(f, GP(ARG(0)), 12);
    ret_floats3(c, f);
}
/* SensorFusionOrientation::GetRotationMatrix() const -> a 3x3 matrix, by the result pointer (x8) */
HLE(_ZNK2nn3hid23SensorFusionOrientation17GetRotationMatrixEv) {
    float q[4] = {0, 0, 0, 1}, m[9];
    if (hos_guest_range(ARG(0) + 0x30, 16)) memcpy(q, GP(ARG(0) + 0x30), 16);
    matrix_of(q, m);
    if (hos_guest_range(c->x[8], sizeof m)) {
        memcpy(GP(c->x[8]), m, sizeof m);
        HOS_WRITTEN(GP(c->x[8]), sizeof m);
    }
}
