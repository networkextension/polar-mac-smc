/*
 * smcfan - force SMC fan speed to a fixed RPM (for machines whose SMC
 *          cannot manage fan speed automatically).
 *
 * Usage:
 *   smcfan status        show fan count, mode, target/actual RPM
 *   smcfan set <rpm>     force all fans to <rpm> (manual mode)
 *   smcfan auto          return all fans to automatic mode
 *
 * Works on Intel (fpe2 targets) and Apple Silicon (flt targets).
 * Requires root for writes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

#define KERNEL_INDEX_SMC   2
#define SMC_CMD_READ_KEY   5
#define SMC_CMD_WRITE_KEY  6
#define SMC_CMD_KEY_INFO   9

typedef struct { char major; char minor; char build; char reserved; UInt16 release; } SMCVers_t;
typedef struct { UInt16 version; UInt16 length; UInt32 cpuPLimit; UInt32 gpuPLimit; UInt32 memPLimit; } SMCPLimit_t;
typedef struct { UInt32 dataSize; UInt32 dataType; char dataAttributes; } SMCKeyInfo_t;

typedef struct {
    UInt32       key;
    SMCVers_t    vers;
    SMCPLimit_t  pLimitData;
    SMCKeyInfo_t keyInfo;
    char         result;
    char         status;
    char         data8;
    UInt32       data32;
    unsigned char bytes[32];
} SMCKeyData_t;

static io_connect_t g_conn = 0;

static UInt32 str2key(const char *s)
{
    return ((UInt32)s[0] << 24) | ((UInt32)s[1] << 16) | ((UInt32)s[2] << 8) | (UInt32)s[3];
}

static void type2str(UInt32 t, char out[5])
{
    out[0] = (t >> 24) & 0xff; out[1] = (t >> 16) & 0xff;
    out[2] = (t >> 8) & 0xff;  out[3] = t & 0xff; out[4] = 0;
}

static kern_return_t smc_call(SMCKeyData_t *in, SMCKeyData_t *out)
{
    size_t outsize = sizeof(SMCKeyData_t);
    return IOConnectCallStructMethod(g_conn, KERNEL_INDEX_SMC,
                                     in, sizeof(SMCKeyData_t), out, &outsize);
}

/* Returns 0 on success, fills info. */
static int smc_key_info(const char *key, SMCKeyInfo_t *info)
{
    SMCKeyData_t in = {0}, out = {0};
    in.key = str2key(key);
    in.data8 = SMC_CMD_KEY_INFO;
    if (smc_call(&in, &out) != kIOReturnSuccess || out.result != 0)
        return -1;
    *info = out.keyInfo;
    return 0;
}

static int smc_read(const char *key, SMCKeyInfo_t *info, unsigned char bytes[32])
{
    if (smc_key_info(key, info) != 0)
        return -1;
    SMCKeyData_t in = {0}, out = {0};
    in.key = str2key(key);
    in.keyInfo.dataSize = info->dataSize;
    in.data8 = SMC_CMD_READ_KEY;
    if (smc_call(&in, &out) != kIOReturnSuccess || out.result != 0)
        return -1;
    memcpy(bytes, out.bytes, 32);
    return 0;
}

static int smc_write(const char *key, const unsigned char *bytes, UInt32 size)
{
    SMCKeyData_t in = {0}, out = {0};
    in.key = str2key(key);
    in.keyInfo.dataSize = size;
    in.data8 = SMC_CMD_WRITE_KEY;
    memcpy(in.bytes, bytes, size);
    if (smc_call(&in, &out) != kIOReturnSuccess || out.result != 0)
        return -1;
    return 0;
}

/* Decode an RPM value according to the key's data type. */
static double decode_rpm(const SMCKeyInfo_t *info, const unsigned char *b)
{
    char t[5]; type2str(info->dataType, t);
    if (strcmp(t, "flt ") == 0) {
        float f; memcpy(&f, b, 4); return (double)f;
    }
    if (strcmp(t, "fpe2") == 0)
        return ((b[0] << 8) | b[1]) / 4.0;
    if (strcmp(t, "ui16") == 0)
        return (double)((b[0] << 8) | b[1]);
    if (strcmp(t, "ui8 ") == 0)
        return (double)b[0];
    return -1;
}

/* Encode an RPM into bytes per the key's data type. Returns size or -1. */
static int encode_rpm(const SMCKeyInfo_t *info, double rpm, unsigned char *b)
{
    char t[5]; type2str(info->dataType, t);
    if (strcmp(t, "flt ") == 0) {
        float f = (float)rpm; memcpy(b, &f, 4); return 4;
    }
    if (strcmp(t, "fpe2") == 0) {
        UInt16 v = (UInt16)(rpm * 4.0); b[0] = v >> 8; b[1] = v & 0xff; return 2;
    }
    return -1;
}

static int fan_count(void)
{
    SMCKeyInfo_t info; unsigned char b[32];
    if (smc_read("FNum", &info, b) != 0)
        return -1;
    return b[0];
}

/* Write fan mode key F<n>Md (1 = forced/manual, 0 = auto).
 * Falls back to legacy "FS! " bitmask on old Intel SMCs. */
static int set_fan_mode(int fan, int forced)
{
    char key[5]; SMCKeyInfo_t info;
    snprintf(key, sizeof(key), "F%dMd", fan);
    if (smc_key_info(key, &info) == 0) {
        unsigned char b[1] = { (unsigned char)(forced ? 1 : 0) };
        return smc_write(key, b, 1);
    }
    /* legacy: FS! is a ui16 bitmask, one bit per fan */
    unsigned char cur[32];
    if (smc_read("FS! ", &info, cur) != 0)
        return -1;
    UInt16 mask = (cur[0] << 8) | cur[1];
    if (forced) mask |= (1 << fan); else mask &= ~(1 << fan);
    unsigned char b[2] = { (unsigned char)(mask >> 8), (unsigned char)(mask & 0xff) };
    return smc_write("FS! ", b, 2);
}

static int set_fan_target(int fan, double rpm)
{
    char key[5]; SMCKeyInfo_t info; unsigned char b[32] = {0};
    snprintf(key, sizeof(key), "F%dTg", fan);
    if (smc_key_info(key, &info) != 0) {
        fprintf(stderr, "smcfan: no target key %s\n", key);
        return -1;
    }
    int size = encode_rpm(&info, rpm, b);
    if (size < 0) {
        char t[5]; type2str(info.dataType, t);
        fprintf(stderr, "smcfan: unsupported type '%s' for %s\n", t, key);
        return -1;
    }
    return smc_write(key, b, (UInt32)size);
}

static double read_fan_key(int fan, const char *suffix)
{
    char key[5]; SMCKeyInfo_t info; unsigned char b[32];
    snprintf(key, sizeof(key), "F%d%s", fan, suffix);
    if (smc_read(key, &info, b) != 0)
        return -1;
    return decode_rpm(&info, b);
}

static int cmd_status(void)
{
    int n = fan_count();
    if (n < 0) { fprintf(stderr, "smcfan: cannot read FNum\n"); return 1; }
    printf("fans: %d\n", n);
    for (int i = 0; i < n; i++) {
        double ac = read_fan_key(i, "Ac");
        double tg = read_fan_key(i, "Tg");
        double mn = read_fan_key(i, "Mn");
        double mx = read_fan_key(i, "Mx");
        double md = read_fan_key(i, "Md");
        printf("fan%d: actual=%.0f target=%.0f min=%.0f max=%.0f mode=%s\n",
               i, ac, tg, mn, mx,
               md < 0 ? "?" : (md > 0 ? "forced" : "auto"));
    }
    return 0;
}

static int cmd_set(double rpm)
{
    int n = fan_count();
    if (n < 0) { fprintf(stderr, "smcfan: cannot read FNum\n"); return 1; }
    int rc = 0;
    for (int i = 0; i < n; i++) {
        /* clamp into the fan's supported range */
        double mn = read_fan_key(i, "Mn");
        double mx = read_fan_key(i, "Mx");
        double r = rpm;
        if (mn > 0 && r < mn) r = mn;
        if (mx > 0 && r > mx) r = mx;
        if (set_fan_mode(i, 1) != 0) {
            fprintf(stderr, "smcfan: fan%d: failed to set forced mode\n", i);
            rc = 1; continue;
        }
        if (set_fan_target(i, r) != 0) {
            fprintf(stderr, "smcfan: fan%d: failed to set target %.0f\n", i, r);
            rc = 1; continue;
        }
        printf("fan%d: forced to %.0f rpm\n", i, r);
    }
    return rc;
}

static int cmd_auto(void)
{
    int n = fan_count();
    if (n < 0) { fprintf(stderr, "smcfan: cannot read FNum\n"); return 1; }
    int rc = 0;
    for (int i = 0; i < n; i++) {
        if (set_fan_mode(i, 0) != 0) {
            fprintf(stderr, "smcfan: fan%d: failed to restore auto mode\n", i);
            rc = 1;
        } else {
            printf("fan%d: auto\n", i);
        }
    }
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s status | set <rpm> | auto\n", argv[0]);
        return 2;
    }

    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault,
                                                   IOServiceMatching("AppleSMC"));
    if (!svc) { fprintf(stderr, "smcfan: AppleSMC service not found\n"); return 1; }
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &g_conn);
    IOObjectRelease(svc);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "smcfan: IOServiceOpen failed (0x%x)\n", kr);
        return 1;
    }

    int rc;
    if (strcmp(argv[1], "status") == 0)
        rc = cmd_status();
    else if (strcmp(argv[1], "set") == 0 && argc >= 3)
        rc = cmd_set(atof(argv[2]));
    else if (strcmp(argv[1], "auto") == 0)
        rc = cmd_auto();
    else {
        fprintf(stderr, "usage: %s status | set <rpm> | auto\n", argv[0]);
        rc = 2;
    }

    IOServiceClose(g_conn);
    return rc;
}
