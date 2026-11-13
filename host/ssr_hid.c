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

/* (diagnostic) each place the game calls a motion function from, once: which code reads the motion when */
static void hid_seen(Ctx *c, const char *fn, uint32_t handle) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static struct { const char *fn; uint64_t lr; } seen[48];
    static int n;
    const uint64_t lr = c->x[30];
    AcquireSRWLockExclusive(&lock);
    int i = 0;
    while (i < n && !(seen[i].fn == fn && seen[i].lr == lr)) i++;
    const int fresh = i == n && n < 48;
    if (fresh) seen[n].fn = fn, seen[n].lr = lr, n++;
    ReleaseSRWLockExclusive(&lock);
    if (fresh) hos_log("hid: %s (handle %#x) called from lr %#llx", fn, handle, (unsigned long long)lr);
}

/* ---- six-axis extras ------------------------------------------------------------------------------------------- */
/* IsSixAxisSensorAtRest(handle) -> bool: the motion pad lying still (pad_sdl.c's filter). Switch Sports' calibration
   asks for the Joy-Con laid on a table and waits for this */
HLE(_ZN2nn3hid21IsSixAxisSensorAtRestERKNS0_19SixAxisSensorHandleE) {
    int hos_sdl_motion_still(float stats[3]);
    float st[3];
    const int still = hos_sdl_motion_still(st);
    static int was = -1;
    static ULONGLONG said;
    const ULONGLONG now = GetTickCount64();
    const uint32_t handle = hos_guest_range(ARG(0), 4) ? G32(ARG(0)) : 0;
    hid_seen(c, "IsSixAxisSensorAtRest", handle);
    /* (the change, and every 2 s while it is asked: the sensors' noise, to set HOS_MOTION_REST_* from a real Joy-Con) */
    if (still != was || now - said >= 2000) {
        hos_log("hid: at rest? %s (handle %#x; gyro spread %.4f rad/s, accel spread %.4f G, |accel| %.3f G)",
                still ? "yes" : "no", handle, st[0], st[1], st[2]);
        was = still, said = now;
    }
    RET(still);
}
/* GetGyroscopeZeroDriftMode(handle) -> GyroscopeZeroDriftMode: 1 standard (what the motion filter does) */
HLE(_ZN2nn3hid25GetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleE) { RET(1); }
HLE(_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE) {}

/* ---- sensor fusion ---------------------------------------------------------------------------------------------- */
#define FUSION_HANDLE 0x5346u   /* ("SF") */
#define ENTRY 0x50
static volatile int fusion_on[2];   /* (each Joy-Con of the pair: its own handle, FUSION_HANDLE + 0 left, + 1 right, both
                                       the one motion pad's; the game turns them on and off one at a time) */
static int fusion_slot(uint32_t h) { return h == FUSION_HANDLE + 1 ? 1 : h == FUSION_HANDLE ? 0 : -1; }
static float yaw_fix[4] = {0, 0, 0, 1};   /* applied on the world's side (ResetSensorFusionYaw) */
static SRWLOCK fusion_lock = SRWLOCK_INIT;
static int64_t fusion_base, fusion_last;
static int fusion_was_live;

HLE(_ZN2nn3hid25GetSensorFusionMemorySizeEv) { RET(0x1000); }
HLE(_ZN2nn3hid29GetSensorFusionWorkBufferSizeEv) { RET(0x1000); }
HLE(_ZN2nn3hid22InitializeSensorFusionEPvm) { RET(0); }
HLE(_ZN2nn3hid20FinalizeSensorFusionEv) { fusion_on[0] = fusion_on[1] = 0; hos_log("hid: sensor fusion finalized"); }
/* GetSensorFusionHandles(SensorFusionHandle*, int, unsigned int const& npad, BitFlagSet styles) -> int: No.1's, one per
   Joy-Con a pair has (left, right: the game picks min(count - 1, its Joy-Con's index)), else one */
HLE(_ZN2nn3hid22GetSensorFusionHandlesEPNS0_18SensorFusionHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE) {
    static int logged;
    if (logged++ < 4) hos_log("hid: sensor fusion handles asked for: npad %u, %d wanted, styles %#x", G32(ARG(2)), (int)ARG(1),
                              (uint32_t)ARG(3));
    if ((int)ARG(1) < 1 || G32(ARG(2)) != NPAD_NO1) { RET(0); return; }
    const int n = (int)ARG(1) >= 2 && ((uint32_t)ARG(3) & STYLE_JOYDUAL) ? 2 : 1;
    for (int i = 0; i < n; i++) G32(ARG(0) + 4 * (uint64_t)i) = FUSION_HANDLE + (uint32_t)i;
    RET(n);
}
HLE(_ZN2nn3hid18EnableSensorFusionENS0_18SensorFusionHandleEPvm) {
    const int k = fusion_slot((uint32_t)ARG(0));
    hid_seen(c, "EnableSensorFusion", (uint32_t)ARG(0));
    if (k < 0) { RET(0); return; }
    if (!fusion_on[k]) hos_log("hid: sensor fusion on for the %s Joy-Con (the orientation from the motion controls)", k ? "right" : "left");
    fusion_on[k] = 1;
    RET(0);
}
HLE(_ZN2nn3hid19DisableSensorFusionENS0_18SensorFusionHandleE) {
    const int k = fusion_slot((uint32_t)ARG(0));
    hid_seen(c, "DisableSensorFusion", (uint32_t)ARG(0));
    if (k >= 0 && fusion_on[k]) hos_log("hid: sensor fusion off for the %s Joy-Con", k ? "right" : "left");
    if (k >= 0) fusion_on[k] = 0;
    RET(0);
}
HLE(_ZN2nn3hid21IsSensorFusionEnabledENS0_18SensorFusionHandleE) {
    const int k = fusion_slot((uint32_t)ARG(0));
    hid_seen(c, "IsSensorFusionEnabled", (uint32_t)ARG(0));
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
static void fused(const HosMotionState *m, float q[4]) {
    float d[4];
    device_quat(m->direction, d);
    AcquireSRWLockShared(&fusion_lock);
    quat_mul(yaw_fix, d, q);
    ReleaseSRWLockShared(&fusion_lock);
}
/* The orientation integrated from the very angular velocity and acceleration handed out with it (+0x24, +0x18), so the
   three always agree: the game steered right from those before it took the Joy-Con for calibrated, and wrong from an
   orientation built another way. The Joy-Con's own rates turn it (q' = q (x) w dt, q the Joy-Con's turn into the world),
   and the accelerometer pulls its tilt toward gravity (Mahony, as the console's fusion corrects drift); lying flat (the
   accelerometer reading 0, 0, -1 G) is no turn, and the yaw is the game's to reset. BUTTERFLY_MOTION_FLIP set: the
   direction-matrix orientation instead (the earlier tries). */
static SRWLOCK integ_lock = SRWLOCK_INIT;
static float integ_q[4] = {0, 0, 0, 1};
static float integ_ring[HOS_MOTION_HISTORY][4];
static int64_t integ_n = -1;   /* the newest reading integrated (its own count) */
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
/* the turn that takes (0, 0, -1) to the reading a (the tilt alone, no yaw) */
static void q_from_accel(const float *a, float q[4]) {
    const float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 1e-3f) { q[0] = q[1] = q[2] = 0, q[3] = 1; return; }
    /* the Joy-Con's turn R has R^T (0,0,-1) = a/|a|: q is the turn from a/|a| to (0, 0, -1) */
    const float u[3] = {a[0] / n, a[1] / n, a[2] / n}, v[3] = {0, 0, -1};
    const float d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    if (d < -0.9999f) { q[0] = 1, q[1] = q[2] = q[3] = 0; return; }
    q[0] = u[1] * v[2] - u[2] * v[1], q[1] = u[2] * v[0] - u[0] * v[2], q[2] = u[0] * v[1] - u[1] * v[0], q[3] = 1 + d;
    q_norm(q);
}
static void integrate_one(const HosMotionSample *s) {
    const HosMotionState *m = &s->st;
    float dt = s->dt_ns > 0 ? (float)s->dt_ns * 1e-9f : 0.005f;
    if (dt > 0.1f) dt = 0.1f;
    float w[3];
    for (int k = 0; k < 3; k++) w[k] = m->gyro[k] * 6.28318531f;   /* (turns a second: radians a second) */
    const float an = sqrtf(m->accel[0] * m->accel[0] + m->accel[1] * m->accel[1] + m->accel[2] * m->accel[2]);
    if (an > 0.8f && an < 1.2f) {
        float r[9];
        matrix_of(integ_q, r);
        const float v[3] = {-r[6], -r[7], -r[8]};   /* the reading lying this way would give: R^T (0, 0, -1) */
        const float a[3] = {m->accel[0] / an, m->accel[1] / an, m->accel[2] / an};
        const float e[3] = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};
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
static void integrated(const HosMotionSample *h, int live, float qs[][4]) {
    AcquireSRWLockExclusive(&integ_lock);
    if (integ_n < 0 || h[0].n < integ_n) {
        q_from_accel(h[live - 1].st.accel, integ_q);   /* (first, or a new motion pad: the tilt from gravity; a gap is
                                                          integrated over from what is left of it) */
        integ_n = h[live - 1].n;
        memcpy(integ_ring[integ_n % HOS_MOTION_HISTORY], integ_q, 16);
    }
    for (int i = live - 1; i >= 0; i--) {
        if (h[i].n <= integ_n) continue;
        integrate_one(&h[i]);
        integ_n = h[i].n;
        memcpy(integ_ring[integ_n % HOS_MOTION_HISTORY], integ_q, 16);
    }
    for (int i = 0; i < live; i++) {
        if (integ_n - h[i].n < HOS_MOTION_HISTORY) memcpy(qs[i], integ_ring[h[i].n % HOS_MOTION_HISTORY], 16);
        else memcpy(qs[i], integ_q, 16);
    }
    ReleaseSRWLockExclusive(&integ_lock);
}
/* ResetSensorFusionYaw(handle): the way the Joy-Con points now is straight ahead */
HLE(_ZN2nn3hid20ResetSensorFusionYawENS0_18SensorFusionHandleE) {
    hid_seen(c, "ResetSensorFusionYaw", (uint32_t)ARG(0));
    int hos_sdl_motion_history(HosMotionSample *out, int max);   /* pad_sdl.c */
    HosMotionSample h[1];
    if (hos_sdl_motion_history(h, 1) == 1) {
        float d[4], m[9];
        if (use_integration()) {
            AcquireSRWLockShared(&integ_lock);
            memcpy(d, integ_q, sizeof d);
            ReleaseSRWLockShared(&integ_lock);
        } else {
            device_quat(h[0].st.direction, d);
        }
        matrix_of(d, m);
        const float yaw = atan2f(m[3], m[0]);   /* (the Joy-Con's x axis in the world: column 0) */
        AcquireSRWLockExclusive(&fusion_lock);
        yaw_fix[0] = yaw_fix[1] = 0, yaw_fix[2] = sinf(-yaw / 2), yaw_fix[3] = cosf(-yaw / 2);
        ReleaseSRWLockExclusive(&fusion_lock);
    }
    RET(0);
}
/* GetSensorFusionOrientations(SensorFusionOrientation* out, int max, SensorFusionHandle) -> int: newest first */
HLE(_ZN2nn3hid27GetSensorFusionOrientationsEPNS0_23SensorFusionOrientationEiNS0_18SensorFusionHandleE) {
    hid_seen(c, "GetSensorFusionOrientations", (uint32_t)ARG(2));
    const uint64_t out = ARG(0);
    const int max = (int)ARG(1) < 1 ? 0 : (int)ARG(1) > HOS_MOTION_HISTORY ? HOS_MOTION_HISTORY : (int)ARG(1);
    if (!max || !hos_guest_range(out, (uint64_t)max * ENTRY)) { RET(0); return; }
    int hos_sdl_motion_history(HosMotionSample *out, int max);   /* pad_sdl.c */
    HosMotionSample h[HOS_MOTION_HISTORY];
    const int live = hos_sdl_motion_history(h, max);
    /* (sampling numbers as GetSixAxisSensorStates hands them out: always on, a whole history apart between live and rest) */
    if (live) {
        if (!fusion_was_live || fusion_base + h[0].n < fusion_last) fusion_base = fusion_last + HOS_MOTION_HISTORY - h[0].n;
        fusion_last = fusion_base + h[0].n;
    } else {
        fusion_last += fusion_was_live ? HOS_MOTION_HISTORY : 1;
    }
    fusion_was_live = live > 0;
    /* status (2 bits): bit 1 the sensors calibrated (the game's calibration screen waits for it, 0x80c7bd00) */
    int hos_sdl_motion_calibrated(void);   /* pad_sdl.c */
    const int status = live && hos_sdl_motion_calibrated() ? 2 : 0;
    static const HosMotionState at_rest = {{0, 0, -1}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    const int n = live ? live : 1;
    static float qs[HOS_MOTION_HISTORY][4];   /* (under the history's own order: one caller at a time below) */
    static SRWLOCK out_lock = SRWLOCK_INIT;
    const int integ = use_integration() && live;
    AcquireSRWLockExclusive(&out_lock);
    if (integ) integrated(h, live, qs);
    for (int i = 0; i < n; i++) {
        const uint64_t e = out + (uint64_t)i * ENTRY;
        const HosMotionState *m = live ? &h[i].st : &at_rest;
        float q[4], dps[3];
        if (integ) {
            AcquireSRWLockShared(&fusion_lock);
            quat_mul(yaw_fix, qs[i], q);
            ReleaseSRWLockShared(&fusion_lock);
        } else {
            fused(m, q);
        }
        for (int k = 0; k < 3; k++) dps[k] = m->gyro[k] * 360.0f;
        memset(GP(e), 0, ENTRY);
        G64(e) = (uint64_t)(live ? fusion_base + h[i].n : fusion_last - i);
        G64(e + 8) = 1;
        G64(e + 0x10) = (uint64_t)(live && h[i].dt_ns > 0 ? h[i].dt_ns : 5000000);
        memcpy(GP(e + 0x18), m->accel, 12);
        memcpy(GP(e + 0x24), dps, 12);
        memcpy(GP(e + 0x30), q, 16);
        *(uint8_t *)GP(e + 0x40) = (uint8_t)status;
    }
    ReleaseSRWLockExclusive(&out_lock);
    HOS_WRITTEN(GP(out), (size_t)n * ENTRY);
    static int logged;
    if (!logged++) hos_log("hid: sensor fusion: first orientations (%s)", live ? "a pad with motion" : "no pad with motion: at rest");
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
