/* (butterfly) nn::mii::Database without the mii:e / mii:u services: Nintendo Switch Sports Resort builds its CPU
   players with Database::BuildRandom and reads the console's Miis with Database::Get. Left to the SDK, each call goes
   through the service object Initialize gets over IPC; the runtime answers that empty, the object stays null and the
   first call reads through it.

   The Miis: the ones in an Eden-style Mii database (MiiDatabase.dat, 'NFDB': the file Eden, Lichii and Fritchii keep in
   nand\system\save\8000000000000030), then the console's six default Miis. The database is read from the game folder,
   else from the emulators' folders in %APPDATA%. Nothing is written back.

   Random and default Miis follow Eden's service (src/core/hle/service/mii, GPL-3.0-or-later; its tables, from Ryujinx,
   MIT, are in nn_mii_tables.h). */
#include "hos.h"
#include <windows.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nn_mii_tables.h"

extern const char *hos_romfs_dir;

/* nn::mii::CharInfo (0x58) and CharInfoElement (CharInfo + Source, 0x5c) */
typedef struct {
    uint8_t create_id[16];
    uint16_t name[10];
    uint16_t null_terminator;
    uint8_t font_region, favorite_color, gender, height, build, type, region_move;
    uint8_t faceline_type, faceline_color, faceline_wrinkle, faceline_make;
    uint8_t hair_type, hair_color, hair_flip;
    uint8_t eye_type, eye_color, eye_scale, eye_aspect, eye_rotate, eye_x, eye_y;
    uint8_t eyebrow_type, eyebrow_color, eyebrow_scale, eyebrow_aspect, eyebrow_rotate, eyebrow_x, eyebrow_y;
    uint8_t nose_type, nose_scale, nose_y;
    uint8_t mouth_type, mouth_color, mouth_scale, mouth_aspect, mouth_y;
    uint8_t beard_color, beard_type, mustache_type, mustache_scale, mustache_y;
    uint8_t glass_type, glass_color, glass_scale, glass_y;
    uint8_t mole_type, mole_scale, mole_x, mole_y;
    uint8_t padding;
} CharInfo;
_Static_assert(sizeof(CharInfo) == 0x58, "CharInfo");
#define ELEMENT_SIZE 0x5c
enum { SOURCE_DATABASE = 0, SOURCE_DEFAULT = 1 };
enum { FLAG_DATABASE = 1, FLAG_DEFAULT = 2 };

/* ---- random numbers: Eden's GetRandomValue(min, max) includes max ----------------------------------------------- */
static uint64_t rng_state;
static uint32_t rnd32(void) {
    if (!rng_state) {
        rng_state = ((uint64_t)time(NULL) * 1000003u + (uint64_t)clock()) * 0x9E3779B97F4A7C15ull | 1;
    }
    rng_state ^= rng_state << 13;   /* xorshift64 */
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}
static uint32_t rnd_incl(uint32_t max) { return rnd32() % (max + 1); }   /* 0..max */
static uint32_t rnd_below(uint32_t n) { return n ? rnd32() % n : 0; }   /* 0..n-1 */

static void make_create_id(uint8_t id[16]) {   /* a random RFC 4122 version 4 UUID */
    for (int i = 0; i < 16; i++) id[i] = (uint8_t)rnd32();
    id[6] = (uint8_t)((id[6] & 0x0f) | 0x40);
    id[8] = (uint8_t)((id[8] & 0x3f) | 0x80);
}
static void set_name(CharInfo *ci, const uint16_t *name) {
    memset(ci->name, 0, sizeof ci->name);
    for (int i = 0; i < 10 && name[i]; i++) ci->name[i] = name[i];
    ci->null_terminator = 0;
}
static const uint16_t NO_NAME[10] = {'n', 'o', ' ', 'n', 'a', 'm', 'e'};

/* ---- Eden's CoreData::BuildRandom, straight into a CharInfo --------------------------------------------------------- */
#define PICK(info) ((uint8_t)(info).values[rnd_below((info).values_count)])
static void build_random(CharInfo *ci, uint32_t age, uint32_t gender, uint32_t race) {
    if (gender > 1) gender = rnd_incl(1);
    if (age > 2) { const uint32_t r = rnd_incl(10); age = r >= 8 ? 2 : r >= 4 ? 1 : 0; }
    if (race > 2) { const uint32_t r = rnd_incl(10); race = r >= 8 ? 0 : r >= 4 ? 1 : 2; }

    memset(ci, 0, sizeof *ci);
    set_name(ci, NO_NAME);
    ci->gender = (uint8_t)gender;
    ci->favorite_color = (uint8_t)rnd_incl(11);
    ci->height = 64;
    ci->build = 64;

    const uint32_t axis_y = gender == 1 && age == 0 ? rnd_incl(3) : 0;
    const size_t index = 9 * gender + 3 * race + age;

    ci->faceline_type = PICK(mii_RandomMiiFaceline[index]);
    ci->faceline_color = PICK(mii_RandomMiiFacelineColor[3 * gender + race]);
    ci->faceline_wrinkle = PICK(mii_RandomMiiFacelineWrinkle[index]);
    ci->faceline_make = PICK(mii_RandomMiiFacelineMakeup[index]);

    ci->hair_type = PICK(mii_RandomMiiHairType[index]);
    ci->hair_color = mii_Ver3HairColorTable[PICK(mii_RandomMiiHairColor[3 * race + age]) & 7];
    ci->hair_flip = (uint8_t)rnd_incl(1);

    ci->eye_type = PICK(mii_RandomMiiEyeType[index]);
    const int eye_rotate_1 = gender != 0 ? 4 : 2, eye_rotate_2 = gender != 0 ? 3 : 4;
    const int eye_rotate_offset = 32 - mii_EyeRotateLookup[eye_rotate_1] + eye_rotate_2;
    const int eye_rotate = 32 - mii_EyeRotateLookup[ci->eye_type % 62];
    ci->eye_color = mii_Ver3EyeColorTable[PICK(mii_RandomMiiEyeColor[race]) % 6];
    ci->eye_scale = 4;
    ci->eye_aspect = 3;
    ci->eye_rotate = (uint8_t)(eye_rotate_offset - eye_rotate);
    ci->eye_x = 2;
    ci->eye_y = (uint8_t)(axis_y + 12);

    ci->eyebrow_type = PICK(mii_RandomMiiEyebrowType[index]);
    const int eyebrow_rotate_1 = race == 2 ? 6 : 0, eyebrow_y = race == 2 ? 6 : 7;
    const int eyebrow_rotate_offset = 32 - mii_EyebrowRotateLookup[eyebrow_rotate_1] + 6;
    const int eyebrow_rotate = 32 - mii_EyebrowRotateLookup[ci->eyebrow_type % 24];
    ci->eyebrow_color = ci->hair_color;
    ci->eyebrow_scale = 4;
    ci->eyebrow_aspect = 3;
    ci->eyebrow_rotate = (uint8_t)(eyebrow_rotate_offset - eyebrow_rotate);
    ci->eyebrow_x = 2;
    ci->eyebrow_y = (uint8_t)(axis_y + eyebrow_y);

    ci->nose_type = PICK(mii_RandomMiiNoseType[index]);
    ci->nose_scale = gender == 1 ? 3 : 4;
    ci->nose_y = (uint8_t)(axis_y + 9);

    const uint32_t mouth_color = gender == 1 ? rnd_incl(4) : 0;
    ci->mouth_type = PICK(mii_RandomMiiMouthType[index]);
    ci->mouth_color = mii_Ver3MouthColorTable[mouth_color % 5];
    ci->mouth_scale = 4;
    ci->mouth_aspect = 3;
    ci->mouth_y = (uint8_t)(axis_y + 13);

    ci->beard_color = ci->hair_color;
    ci->mustache_scale = 4;
    if (gender == 0 && age != 0 && rnd_incl(10) < 2) {
        const uint32_t flag = rnd_incl(3);   /* bit 0 beard, bit 1 mustache */
        ci->beard_type = flag & 1 ? (uint8_t)(1 + rnd_incl(4)) : 0;
        ci->mustache_type = flag & 2 ? (uint8_t)(1 + rnd_incl(4)) : 0;
        ci->mustache_y = 10;
    } else {
        ci->mustache_type = ci->beard_type = 0;
        ci->mustache_y = (uint8_t)(axis_y + 10);
    }

    const MiiRandomMiiData2 *glasses = &mii_RandomMiiGlassType[age];
    const uint32_t glasses_start = rnd_incl(100);
    uint8_t glass_type = 0;
    while (glasses_start < glasses->values[glass_type])
        if (++glass_type >= glasses->values_count) { glass_type = 0; break; }
    ci->glass_type = glass_type;
    ci->glass_color = mii_Ver3GlassColorTable[0];
    ci->glass_scale = 4;
    ci->glass_y = (uint8_t)(axis_y + 10);

    ci->mole_type = 0;
    ci->mole_scale = 4;
    ci->mole_x = 2;
    ci->mole_y = 20;
    make_create_id(ci->create_id);
}

/* ---- Eden's StoreData::BuildDefault ---------------------------------------------------------------------------------- */
static void build_default(CharInfo *ci, int index) {
    const MiiDefault *d = &mii_DefaultMii[index];
    memset(ci, 0, sizeof *ci);
    ci->faceline_type = (uint8_t)d->face_type;
    ci->faceline_color = mii_Ver3FacelineColorTable[d->face_color & 7];
    ci->faceline_wrinkle = (uint8_t)d->face_wrinkle;
    ci->faceline_make = (uint8_t)d->face_makeup;
    ci->hair_type = (uint8_t)d->hair_type;
    ci->hair_color = mii_Ver3HairColorTable[d->hair_color & 7];
    ci->hair_flip = (uint8_t)d->hair_flip;
    ci->eye_type = (uint8_t)d->eye_type;
    ci->eye_color = mii_Ver3EyeColorTable[d->eye_color % 6];
    ci->eye_scale = (uint8_t)d->eye_scale;
    ci->eye_aspect = (uint8_t)d->eye_aspect;
    ci->eye_rotate = (uint8_t)d->eye_rotate;
    ci->eye_x = (uint8_t)d->eye_x;
    ci->eye_y = (uint8_t)d->eye_y;
    ci->eyebrow_type = (uint8_t)d->eyebrow_type;
    ci->eyebrow_color = mii_Ver3HairColorTable[d->eyebrow_color & 7];
    ci->eyebrow_scale = (uint8_t)d->eyebrow_scale;
    ci->eyebrow_aspect = (uint8_t)d->eyebrow_aspect;
    ci->eyebrow_rotate = (uint8_t)d->eyebrow_rotate;
    ci->eyebrow_x = (uint8_t)d->eyebrow_x;
    ci->eyebrow_y = (uint8_t)(d->eyebrow_y - 3);
    ci->nose_type = (uint8_t)d->nose_type;
    ci->nose_scale = (uint8_t)d->nose_scale;
    ci->nose_y = (uint8_t)d->nose_y;
    ci->mouth_type = (uint8_t)d->mouth_type;
    ci->mouth_color = mii_Ver3MouthColorTable[d->mouth_color % 5];
    ci->mouth_scale = (uint8_t)d->mouth_scale;
    ci->mouth_aspect = (uint8_t)d->mouth_aspect;
    ci->mouth_y = (uint8_t)d->mouth_y;
    ci->mustache_type = (uint8_t)d->mustache_type;
    ci->beard_type = (uint8_t)d->beard_type;
    ci->beard_color = mii_Ver3HairColorTable[d->beard_color & 7];
    ci->mustache_scale = (uint8_t)d->mustache_scale;
    ci->mustache_y = (uint8_t)d->mustache_y;
    ci->glass_type = (uint8_t)d->glasses_type;
    ci->glass_color = mii_Ver3GlassColorTable[d->glasses_color % 7];
    ci->glass_scale = (uint8_t)d->glasses_scale;
    ci->glass_y = (uint8_t)d->glasses_y;
    ci->mole_type = (uint8_t)d->mole_type;
    ci->mole_scale = (uint8_t)d->mole_scale;
    ci->mole_x = (uint8_t)d->mole_x;
    ci->mole_y = (uint8_t)d->mole_y;
    ci->height = (uint8_t)d->height;
    ci->build = (uint8_t)d->weight;
    ci->gender = (uint8_t)d->gender;
    ci->favorite_color = (uint8_t)d->favorite_color;
    ci->region_move = (uint8_t)d->region_move;
    ci->font_region = (uint8_t)d->font_region;
    ci->type = (uint8_t)d->type;
    set_name(ci, d->nickname);
    /* (a fixed create id per default Mii, so the game sees the same Mii each run) */
    memset(ci->create_id, 0, 16);
    ci->create_id[15] = (uint8_t)(index + 1);
}

/* ---- the Mii database file: 'NFDB', 100 StoreData of 0x44 (Eden's mii_database.h, core_data.h) ------------------- */
#define DB_SIZE 0x1A98
#define DB_MAX 100
static CharInfo db_miis[DB_MAX];
static int db_count = -1;
static SRWLOCK db_lock = SRWLOCK_INIT;

#define BITS(w, at, n) ((uint8_t)(((w) >> (at)) & ((1u << (n)) - 1)))
static int from_store_data(CharInfo *ci, const uint8_t *s) {   /* 0 for a special Mii (not for games) */
    uint32_t w[7];
    memcpy(w, s, sizeof w);
    if (BITS(w[0], 31, 1)) return 0;   /* type 1: special */
    memset(ci, 0, sizeof *ci);
    ci->hair_type = BITS(w[0], 0, 8);       ci->height = BITS(w[0], 8, 7);      ci->mole_type = BITS(w[0], 15, 1);
    ci->build = BITS(w[0], 16, 7);          ci->hair_flip = BITS(w[0], 23, 1);  ci->hair_color = BITS(w[0], 24, 7);
    ci->type = BITS(w[0], 31, 1);
    ci->eye_color = BITS(w[1], 0, 7);       ci->gender = BITS(w[1], 7, 1);      ci->eyebrow_color = BITS(w[1], 8, 7);
    ci->mouth_color = BITS(w[1], 16, 7);    ci->beard_color = BITS(w[1], 24, 7);
    ci->glass_color = BITS(w[2], 0, 7);     ci->eye_type = BITS(w[2], 8, 6);    ci->region_move = BITS(w[2], 14, 2);
    ci->mouth_type = BITS(w[2], 16, 6);     ci->font_region = BITS(w[2], 22, 2); ci->eye_y = BITS(w[2], 24, 5);
    ci->glass_scale = BITS(w[2], 29, 3);
    ci->eyebrow_type = BITS(w[3], 0, 5);    ci->mustache_type = BITS(w[3], 5, 3); ci->nose_type = BITS(w[3], 8, 5);
    ci->beard_type = BITS(w[3], 13, 3);     ci->nose_y = BITS(w[3], 16, 5);     ci->mouth_aspect = BITS(w[3], 21, 3);
    ci->mouth_y = BITS(w[3], 24, 5);        ci->eyebrow_aspect = BITS(w[3], 29, 3);
    ci->mustache_y = BITS(w[4], 0, 5);      ci->eye_rotate = BITS(w[4], 5, 3);  ci->glass_y = BITS(w[4], 8, 5);
    ci->eye_aspect = BITS(w[4], 13, 3);     ci->mole_x = BITS(w[4], 16, 5);     ci->eye_scale = BITS(w[4], 21, 3);
    ci->mole_y = BITS(w[4], 24, 5);
    ci->glass_type = BITS(w[5], 0, 5);      ci->favorite_color = BITS(w[5], 8, 4); ci->faceline_type = BITS(w[5], 12, 4);
    ci->faceline_color = BITS(w[5], 16, 4); ci->faceline_wrinkle = BITS(w[5], 20, 4); ci->faceline_make = BITS(w[5], 24, 4);
    ci->eye_x = BITS(w[5], 28, 4);
    ci->eyebrow_scale = BITS(w[6], 0, 4);   ci->eyebrow_rotate = BITS(w[6], 4, 4); ci->eyebrow_x = BITS(w[6], 8, 4);
    ci->eyebrow_y = BITS(w[6], 12, 4);      ci->nose_scale = BITS(w[6], 16, 4); ci->mouth_scale = BITS(w[6], 20, 4);
    ci->mustache_scale = BITS(w[6], 24, 4); ci->mole_scale = BITS(w[6], 28, 4);
    uint16_t name[10];
    memcpy(name, s + 0x1c, sizeof name);
    set_name(ci, name);
    memcpy(ci->create_id, s + 0x30, 16);
    return 1;
}

static int load_db(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static uint8_t buf[DB_SIZE];
    const size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 4 + 0x44 || memcmp(buf, "NFDB", 4)) {
        hos_log("mii: %s is not a Mii database", path);
        return 0;
    }
    int len = n >= 0x1A96 ? buf[0x1A95] : DB_MAX;
    if (len > DB_MAX) len = DB_MAX;
    db_count = 0;
    for (int i = 0; i < len && 4 + (size_t)(i + 1) * 0x44 <= n; i++)
        if (from_store_data(&db_miis[db_count], buf + 4 + i * 0x44)) db_count++;
    hos_log("mii: %d Miis from %s", db_count, path);
    return 1;
}

static void ensure_db(void) {
    AcquireSRWLockExclusive(&db_lock);
    if (db_count < 0) {
        db_count = 0;
        char path[1024], base[1024];
        /* 1. the game folder's MiiDatabase.dat (beside romfs) */
        snprintf(base, sizeof base, "%s", hos_romfs_dir ? hos_romfs_dir : ".");
        char *sep = strrchr(base, '\\'), *sep2 = strrchr(base, '/');
        if (sep2 > sep) sep = sep2;
        if (sep) *sep = 0;
        else snprintf(base, sizeof base, ".");
        snprintf(path, sizeof path, "%s\\MiiDatabase.dat", base);
        int ok = load_db(path);
        /* 2. an emulator's console (its NAND's Mii save) */
        static const char *const emus[] = {"lichii", "eden", "fritchii", "citron", "yuzu", "suyu"};
        const char *appdata = getenv("APPDATA");
        for (size_t i = 0; !ok && appdata && i < sizeof emus / sizeof *emus; i++) {
            snprintf(path, sizeof path, "%s\\%s\\nand\\system\\save\\8000000000000030\\MiiDatabase.dat", appdata, emus[i]);
            ok = load_db(path);
        }
        if (!ok) hos_log("mii: no Mii database (put a MiiDatabase.dat in %s for your Miis): the default Miis only", base);
    }
    ReleaseSRWLockExclusive(&db_lock);
}

/* ---- the HLEs --------------------------------------------------------------------------------------------------- */
/* Database::Initialize() -> Result: the service object stays null (the destructor then has nothing to close) */
HLE(_ZN2nn3mii8Database10InitializeEv) { ensure_db(); RET(0); }
HLE(_ZN2nn3mii8Database8FinalizeEv) {}
/* Database::IsUpdated(int source_flag) -> bool: the Miis never change while the game runs */
HLE(_ZN2nn3mii8Database9IsUpdatedEi) { RET(0); }
/* Database::Get(int* count, CharInfoElement* out, int capacity, int source_flag) -> Result */
HLE(_ZN2nn3mii8Database3GetEPiPNS0_15CharInfoElementEii) {
    ensure_db();
    const uint64_t out = ARG(2);
    const int cap = (int)ARG(3), flag = (int)ARG(4);
    int n = 0;
    if (cap > 0 && hos_guest_range(out, (uint64_t)cap * ELEMENT_SIZE)) {
        if (flag & FLAG_DATABASE)
            for (int i = 0; i < db_count && n < cap; i++, n++) {
                memcpy(GP(out + (uint64_t)n * ELEMENT_SIZE), &db_miis[i], sizeof(CharInfo));
                G32(out + (uint64_t)n * ELEMENT_SIZE + 0x58) = SOURCE_DATABASE;
            }
        if (flag & FLAG_DEFAULT)
            for (int i = 0; i < 6 && n < cap; i++, n++) {
                CharInfo ci;
                build_default(&ci, i);
                memcpy(GP(out + (uint64_t)n * ELEMENT_SIZE), &ci, sizeof ci);
                G32(out + (uint64_t)n * ELEMENT_SIZE + 0x58) = SOURCE_DEFAULT;
            }
        HOS_WRITTEN(GP(out), (size_t)n * ELEMENT_SIZE);
    }
    if (hos_guest_range(ARG(1), 4)) G32(ARG(1)) = (uint32_t)n;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 4) hos_log("mii: Get(flags %d, room for %d) -> %d Miis", flag, cap, n);
    RET(0);
}
/* Database::GetIndex(int* index, CharInfo const&) const -> Result: the Mii's place in the database, by its create id;
   not there (a default or random Mii, a guest): nn::mii's "not found" (the game then takes -1) */
#define R_MII_NOT_FOUND ((126) | (4 << 9))
HLE(_ZNK2nn3mii8Database8GetIndexEPiRKNS0_8CharInfoE) {
    ensure_db();
    int found = -1;
    if (hos_guest_range(ARG(2), sizeof(CharInfo))) {
        const uint8_t *id = (const uint8_t *)GP(ARG(2));
        for (int i = 0; i < db_count && found < 0; i++)
            if (!memcmp(db_miis[i].create_id, id, 16)) found = i;
    }
    if (found < 0) { RET(R_MII_NOT_FOUND); return; }
    if (hos_guest_range(ARG(1), 4)) G32(ARG(1)) = (uint32_t)found;
    RET(0);
}
/* Database::UpdateLatest(CharInfo* inout, int source_flag) const -> Result: the Miis never change while the game runs, so
   the one handed in is the latest (left as it is) */
HLE(_ZNK2nn3mii8Database12UpdateLatestEPNS0_8CharInfoEi) { RET(0); }
/* Database::BuildRandom(CharInfo*, RandomDesc const& {age, gender, race}) -> Result (the SDK aborts on a failure) */
HLE(_ZN2nn3mii8Database11BuildRandomEPNS0_8CharInfoERKNS0_10RandomDescE) {
    CharInfo ci;
    uint32_t age = 3, gender = 2, race = 3;
    if (hos_guest_range(ARG(2), 12)) { age = G32(ARG(2)); gender = G32(ARG(2) + 4); race = G32(ARG(2) + 8); }
    build_random(&ci, age, gender, race);
    if (hos_guest_range(ARG(1), sizeof ci)) {
        memcpy(GP(ARG(1)), &ci, sizeof ci);
        HOS_WRITTEN(GP(ARG(1)), sizeof ci);
    }
    RET(0);
}
/* The Mii editor (an applet): AppendMii(int* index) and EditAndUpdateMii(CharInfo*, CharInfo const&) report that the
   player backed out. (nn::mii's module is 126; 0x3E7E: its "canceled") */
#define R_MII_CANCELED ((126) | (31 << 9))
HLE(_ZN2nn3mii9AppendMiiEPi) {
    hos_log("mii: AppendMii: there is no Mii editor here");
    RET(R_MII_CANCELED);
}
HLE(_ZN2nn3mii16EditAndUpdateMiiEPNS0_8CharInfoERKS1_) {
    hos_log("mii: EditAndUpdateMii: there is no Mii editor here");
    RET(R_MII_CANCELED);
}
