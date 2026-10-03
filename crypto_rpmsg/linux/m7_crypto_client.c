/*
 * Copyright (c) 2026 Raata <its.raata@gmail.com>
 *
 * Userspace client for the M7 crypto service on /dev/ttyRPMSG*.
 *
 * Interactive menu:
 *   ./m7_crypto_client
 *   ./m7_crypto_client /dev/ttyRPMSG30
 *   ./m7_crypto_client /dev/ttyRPMSG30 menu
 *
 * One-shot CLI (unchanged):
 *   ./m7_crypto_client /dev/ttyRPMSG30 ping
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../m7/m7_crypto_protocol.h"

#ifndef DEFAULT_RPMSG_DEV
#define DEFAULT_RPMSG_DEV "/dev/ttyRPMSG30"
#endif

static uint32_t g_req_id = 1;
static char g_dev[128]   = DEFAULT_RPMSG_DEV;
static int g_fd          = -1;

/* Last encrypt output so decrypt can reuse it without retyping. */
static char g_last_ct_tag_hex[512];

static const char *status_name(uint16_t st)
{
    switch (st)
    {
        case M7CR_OK:
            return "OK";
        case M7CR_ERR_MAGIC:
            return "bad magic";
        case M7CR_ERR_VERSION:
            return "bad version";
        case M7CR_ERR_CMD:
            return "bad cmd";
        case M7CR_ERR_LENGTH:
            return "bad length";
        case M7CR_ERR_NO_KEY:
            return "no key";
        case M7CR_ERR_CRYPTO:
            return "crypto fail";
        case M7CR_ERR_AUTH:
            return "auth fail";
        case M7CR_ERR_BLOB:
            return "blob fail";
        case M7CR_ERR_BUSY:
            return "busy";
        case M7CR_ERR_INTERNAL:
            return "internal";
        default:
            return "unknown";
    }
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *s, uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t n = strlen(s);
    size_t i;
    if (n % 2)
        return -1;
    if (n / 2 > out_cap)
        return -1;
    for (i = 0; i < n / 2; i++)
    {
        int hi = hex_nibble(s[2 * i]);
        int lo = hex_nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = n / 2;
    return 0;
}

static void print_hex(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
    printf("\n");
}

static void hex_to_str(const uint8_t *p, size_t n, char *out, size_t out_cap)
{
    size_t i;
    if (out_cap < (n * 2u + 1u))
    {
        out[0] = '\0';
        return;
    }
    for (i = 0; i < n; i++)
        sprintf(out + (i * 2u), "%02x", p[i]);
    out[n * 2u] = '\0';
}

static void trim_nl(char *s)
{
    size_t n = strlen(s);
    while (n > 0u && (s[n - 1u] == '\n' || s[n - 1u] == '\r'))
    {
        s[n - 1u] = '\0';
        n--;
    }
}

static int read_line(const char *prompt, char *buf, size_t cap)
{
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)cap, stdin) == NULL)
        return -1;
    trim_nl(buf);
    return 0;
}

/* imx_rpmsg_tty is a real tty. Cooked mode rewrites 0x0A/0x0D/0x04 and would
 * corrupt the binary header (ping's command byte is 0x0A). The driver also
 * sends "hello world!" at probe time; drop that leftover before the request. */
static int open_rpmsg(const char *dev)
{
    struct termios tio;
    int fd;
    int flags;
    uint8_t drain[256];

    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;

    if (tcgetattr(fd, &tio) != 0)
    {
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 50; /* 5 s */
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    while (read(fd, drain, sizeof(drain)) > 0)
    {
    }
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags);
    return fd;
}

static void close_rpmsg(void)
{
    if (g_fd >= 0)
    {
        close(g_fd);
        g_fd = -1;
    }
}

static int ensure_open(void)
{
    if (g_fd >= 0)
        return 0;
    g_fd = open_rpmsg(g_dev);
    if (g_fd < 0)
    {
        perror(g_dev);
        printf("Hint: modprobe rpmsg_tty  (or imx_rpmsg_tty), then check ls /dev/ttyRPMSG*\n");
        return -1;
    }
    printf("Opened %s\n", g_dev);
    return 0;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n)
    {
        ssize_t w = write(fd, p, n);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_exact(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n)
    {
        ssize_t r = read(fd, p, n);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

static int transact(int fd, uint16_t cmd, const uint8_t *payload, uint32_t payload_len, uint8_t *rsp_payload,
                    uint32_t rsp_cap, uint32_t *rsp_len, uint16_t *status)
{
    uint8_t req[512];
    uint8_t rsp[512];
    m7cr_req_hdr_t *rh = (m7cr_req_hdr_t *)req;
    m7cr_rsp_hdr_t *sh = (m7cr_rsp_hdr_t *)rsp;

    if (sizeof(m7cr_req_hdr_t) + payload_len > sizeof(req))
        return -1;

    memset(req, 0, sizeof(req));
    rh->magic   = M7CR_MAGIC;
    rh->version = M7CR_VERSION;
    rh->cmd     = cmd;
    rh->req_id  = g_req_id++;
    rh->length  = payload_len;
    if (payload_len && payload)
        memcpy(req + sizeof(*rh), payload, payload_len);

    if (write_all(fd, req, sizeof(*rh) + payload_len) != 0)
        return -1;
    if (read_exact(fd, rsp, sizeof(*sh)) != 0)
        return -1;
    if (sh->magic != M7CR_MAGIC)
    {
        fprintf(stderr, "bad magic 0x%08x (expected 0x%08x)\n", sh->magic, M7CR_MAGIC);
        errno = EPROTO;
        return -1;
    }
    if (sh->length > rsp_cap || sizeof(*sh) + sh->length > sizeof(rsp))
        return -1;
    if (sh->length && read_exact(fd, rsp + sizeof(*sh), sh->length) != 0)
        return -1;

    *status  = sh->status;
    *rsp_len = sh->length;
    if (sh->length && rsp_payload)
        memcpy(rsp_payload, rsp + sizeof(*sh), sh->length);
    return 0;
}

static void print_status(uint16_t status, uint32_t out_len)
{
    printf("status=%u (%s)  payload_len=%u\n", status, status_name(status), out_len);
}

static int do_ping(void)
{
    uint8_t out[64];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (transact(g_fd, M7CR_CMD_PING, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("ping");
        return -1;
    }
    print_status(status, out_len);
    printf("payload=");
    fwrite(out, 1, out_len, stdout);
    printf("\n");
    return status == M7CR_OK ? 0 : 1;
}

static int do_set_channel(void)
{
    char line[128];

    printf("Current channel: %s (%s)\n", g_dev, g_fd >= 0 ? "open" : "closed");
    if (read_line("New device path [/dev/ttyRPMSG30]: ", line, sizeof(line)) != 0)
        return -1;
    if (line[0] == '\0')
        strncpy(line, DEFAULT_RPMSG_DEV, sizeof(line) - 1u);
    close_rpmsg();
    strncpy(g_dev, line, sizeof(g_dev) - 1u);
    g_dev[sizeof(g_dev) - 1u] = '\0';
    return ensure_open();
}

static int do_store_aes(void)
{
    char line[128];
    uint8_t key[16];
    uint8_t out[480];
    size_t n       = 0;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (read_line("AES-128 key (32 hex chars) [00112233445566778899aabbccddeeff]: ", line, sizeof(line)) != 0)
        return -1;
    if (line[0] == '\0')
        strcpy(line, "00112233445566778899aabbccddeeff");
    if (parse_hex(line, key, sizeof(key), &n) || n != M7CR_AES_KEY_LEN)
    {
        printf("Need exactly 32 hex characters (16 bytes).\n");
        return -1;
    }
    if (transact(g_fd, M7CR_CMD_STORE_AES_KEY, key, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("store-aes");
        return -1;
    }
    print_status(status, out_len);
    if (out_len)
    {
        printf("soft_blob=");
        print_hex(out, out_len);
    }
    return status == M7CR_OK ? 0 : 1;
}

static int do_export_aes(void)
{
    char path[256];
    uint8_t out[480];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    FILE *f;

    if (ensure_open() != 0)
        return -1;
    if (read_line("Save AES blob to file [aes.blob]: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0')
        strcpy(path, "aes.blob");
    if (transact(g_fd, M7CR_CMD_EXPORT_AES_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("export-aes");
        return -1;
    }
    print_status(status, out_len);
    if (status != M7CR_OK)
        return 1;
    f = fopen(path, "wb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }
    fwrite(out, 1, out_len, f);
    fclose(f);
    printf("Wrote %u bytes to %s\n", out_len, path);
    return 0;
}

static int do_load_aes(void)
{
    char path[256];
    uint8_t buf[480];
    uint8_t out[64];
    size_t n;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    FILE *f;

    if (ensure_open() != 0)
        return -1;
    if (read_line("Load AES blob from file [aes.blob]: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0')
        strcpy(path, "aes.blob");
    f = fopen(path, "rb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (transact(g_fd, M7CR_CMD_LOAD_AES_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("load-aes");
        return -1;
    }
    print_status(status, out_len);
    return status == M7CR_OK ? 0 : 1;
}

static int do_encrypt_gcm(void)
{
    char ivhex[64];
    char aad[200];
    char pt[200];
    uint8_t buf[480];
    uint8_t out[480];
    size_t ivn         = 0;
    uint16_t aad_len;
    uint16_t pt_len;
    uint32_t plen;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (read_line("IV 12 bytes hex [000000000000000000000000]: ", ivhex, sizeof(ivhex)) != 0)
        return -1;
    if (ivhex[0] == '\0')
        strcpy(ivhex, "000000000000000000000000");
    if (parse_hex(ivhex, buf, 12, &ivn) || ivn != 12)
    {
        printf("IV must be 24 hex chars.\n");
        return -1;
    }
    if (read_line("AAD string (may be empty): ", aad, sizeof(aad)) != 0)
        return -1;
    if (read_line("Plaintext: ", pt, sizeof(pt)) != 0)
        return -1;
    aad_len = (uint16_t)strlen(aad);
    pt_len  = (uint16_t)strlen(pt);
    plen    = 12u + 2u + 2u + aad_len + pt_len;
    if (plen > sizeof(buf))
    {
        printf("Payload too large for RPMsg buffer.\n");
        return -1;
    }
    buf[12] = (uint8_t)(aad_len & 0xff);
    buf[13] = (uint8_t)(aad_len >> 8);
    buf[14] = (uint8_t)(pt_len & 0xff);
    buf[15] = (uint8_t)(pt_len >> 8);
    memcpy(buf + 16, aad, aad_len);
    memcpy(buf + 16 + aad_len, pt, pt_len);
    if (transact(g_fd, M7CR_CMD_ENCRYPT_GCM, buf, plen, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("encrypt-gcm");
        return -1;
    }
    print_status(status, out_len);
    if (status == M7CR_OK && out_len)
    {
        printf("ct_tag=");
        print_hex(out, out_len);
        hex_to_str(out, out_len, g_last_ct_tag_hex, sizeof(g_last_ct_tag_hex));
        printf("(saved for menu decrypt)\n");
    }
    return status == M7CR_OK ? 0 : 1;
}

static int do_decrypt_gcm(void)
{
    char ivhex[64];
    char aad[200];
    char cthex[512];
    uint8_t buf[480];
    uint8_t ctbuf[400];
    uint8_t out[480];
    size_t ivn = 0, ctn = 0;
    uint16_t aad_len;
    uint16_t ct_len;
    uint32_t plen;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (read_line("IV 12 bytes hex [000000000000000000000000]: ", ivhex, sizeof(ivhex)) != 0)
        return -1;
    if (ivhex[0] == '\0')
        strcpy(ivhex, "000000000000000000000000");
    if (parse_hex(ivhex, buf, 12, &ivn) || ivn != 12)
    {
        printf("IV must be 24 hex chars.\n");
        return -1;
    }
    if (read_line("AAD string (may be empty): ", aad, sizeof(aad)) != 0)
        return -1;
    printf("ct+tag hex");
    if (g_last_ct_tag_hex[0] != '\0')
        printf(" [Enter = last encrypt]");
    printf(": ");
    fflush(stdout);
    if (fgets(cthex, (int)sizeof(cthex), stdin) == NULL)
        return -1;
    trim_nl(cthex);
    if (cthex[0] == '\0')
    {
        if (g_last_ct_tag_hex[0] == '\0')
        {
            printf("No previous encrypt result; paste ct+tag hex.\n");
            return -1;
        }
        strncpy(cthex, g_last_ct_tag_hex, sizeof(cthex) - 1u);
    }
    if (parse_hex(cthex, ctbuf, sizeof(ctbuf), &ctn) || ctn < M7CR_GCM_TAG_LEN)
    {
        printf("Invalid ct+tag hex.\n");
        return -1;
    }
    aad_len = (uint16_t)strlen(aad);
    ct_len  = (uint16_t)(ctn - M7CR_GCM_TAG_LEN);
    plen    = 12u + 2u + 2u + aad_len + (uint32_t)ctn;
    if (plen > sizeof(buf))
    {
        printf("Payload too large.\n");
        return -1;
    }
    buf[12] = (uint8_t)(aad_len & 0xff);
    buf[13] = (uint8_t)(aad_len >> 8);
    buf[14] = (uint8_t)(ct_len & 0xff);
    buf[15] = (uint8_t)(ct_len >> 8);
    memcpy(buf + 16, aad, aad_len);
    memcpy(buf + 16 + aad_len, ctbuf, ctn);
    if (transact(g_fd, M7CR_CMD_DECRYPT_GCM, buf, plen, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("decrypt-gcm");
        return -1;
    }
    print_status(status, out_len);
    if (status == M7CR_OK)
    {
        printf("pt=");
        fwrite(out, 1, out_len, stdout);
        printf("\n");
    }
    return status == M7CR_OK ? 0 : 1;
}

static int do_store_hmac(void)
{
    char line[256];
    uint8_t buf[480];
    uint8_t out[480];
    size_t n         = 0;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (read_line("HMAC key hex: ", line, sizeof(line)) != 0)
        return -1;
    if (parse_hex(line, buf + 2, sizeof(buf) - 2, &n) || n == 0 || n > M7CR_HMAC_KEY_MAX)
    {
        printf("Invalid HMAC key hex (1..%u bytes).\n", (unsigned)M7CR_HMAC_KEY_MAX);
        return -1;
    }
    buf[0] = (uint8_t)(n & 0xff);
    buf[1] = (uint8_t)(n >> 8);
    if (transact(g_fd, M7CR_CMD_STORE_HMAC_KEY, buf, (uint32_t)(2 + n), out, sizeof(out), &out_len, &status) != 0)
    {
        perror("store-hmac");
        return -1;
    }
    print_status(status, out_len);
    if (out_len)
    {
        printf("soft_blob=");
        print_hex(out, out_len);
    }
    return status == M7CR_OK ? 0 : 1;
}

static int do_export_hmac(void)
{
    char path[256];
    uint8_t out[480];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    FILE *f;

    if (ensure_open() != 0)
        return -1;
    if (read_line("Save HMAC blob to file [hmac.blob]: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0')
        strcpy(path, "hmac.blob");
    if (transact(g_fd, M7CR_CMD_EXPORT_HMAC_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("export-hmac");
        return -1;
    }
    print_status(status, out_len);
    if (status != M7CR_OK)
        return 1;
    f = fopen(path, "wb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }
    fwrite(out, 1, out_len, f);
    fclose(f);
    printf("Wrote %u bytes to %s\n", out_len, path);
    return 0;
}

static int do_load_hmac(void)
{
    char path[256];
    uint8_t buf[480];
    uint8_t out[64];
    size_t n;
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    FILE *f;

    if (ensure_open() != 0)
        return -1;
    if (read_line("Load HMAC blob from file [hmac.blob]: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0')
        strcpy(path, "hmac.blob");
    f = fopen(path, "rb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (transact(g_fd, M7CR_CMD_LOAD_HMAC_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
    {
        perror("load-hmac");
        return -1;
    }
    print_status(status, out_len);
    return status == M7CR_OK ? 0 : 1;
}

static int do_sign(void)
{
    char data[400];
    uint8_t out[64];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;

    if (ensure_open() != 0)
        return -1;
    if (read_line("Data to sign: ", data, sizeof(data)) != 0)
        return -1;
    if (transact(g_fd, M7CR_CMD_SIGN_HMAC, (const uint8_t *)data, (uint32_t)strlen(data), out, sizeof(out), &out_len,
                 &status) != 0)
    {
        perror("sign");
        return -1;
    }
    print_status(status, out_len);
    if (status == M7CR_OK)
    {
        printf("mac=");
        print_hex(out, out_len);
    }
    return status == M7CR_OK ? 0 : 1;
}

static void print_menu(void)
{
    printf("\n========== M7 Crypto Client ==========\n");
    printf("Channel: %s  (%s)\n", g_dev, g_fd >= 0 ? "open" : "closed");
    printf("--------------------------------------\n");
    printf(" 1) Ping M7\n");
    printf(" 2) Set / open RPMsg channel (ttyRPMSG)\n");
    printf(" 3) Store AES-128 key\n");
    printf(" 4) Export AES soft blob to file\n");
    printf(" 5) Load AES soft blob from file\n");
    printf(" 6) Encrypt AES-GCM\n");
    printf(" 7) Decrypt AES-GCM\n");
    printf(" 8) Store HMAC key\n");
    printf(" 9) Export HMAC soft blob to file\n");
    printf("10) Load HMAC soft blob from file\n");
    printf("11) Sign with HMAC-SHA256\n");
    printf(" 0) Quit\n");
    printf("======================================\n");
}

static int menu_loop(void)
{
    char choice[32];

    g_last_ct_tag_hex[0] = '\0';
    printf("M7 crypto interactive client\n");
    printf("Tip: load the TTY driver once:  modprobe rpmsg_tty\n");

    for (;;)
    {
        print_menu();
        if (read_line("Select: ", choice, sizeof(choice)) != 0)
            break;

        if (!strcmp(choice, "0") || !strcmp(choice, "q") || !strcmp(choice, "Q"))
            break;
        else if (!strcmp(choice, "1"))
            (void)do_ping();
        else if (!strcmp(choice, "2"))
            (void)do_set_channel();
        else if (!strcmp(choice, "3"))
            (void)do_store_aes();
        else if (!strcmp(choice, "4"))
            (void)do_export_aes();
        else if (!strcmp(choice, "5"))
            (void)do_load_aes();
        else if (!strcmp(choice, "6"))
            (void)do_encrypt_gcm();
        else if (!strcmp(choice, "7"))
            (void)do_decrypt_gcm();
        else if (!strcmp(choice, "8"))
            (void)do_store_hmac();
        else if (!strcmp(choice, "9"))
            (void)do_export_hmac();
        else if (!strcmp(choice, "10"))
            (void)do_load_hmac();
        else if (!strcmp(choice, "11"))
            (void)do_sign();
        else
            printf("Unknown choice.\n");
    }

    close_rpmsg();
    printf("Bye.\n");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  Interactive menu:\n"
            "    %s\n"
            "    %s <ttyRPMSG>\n"
            "    %s <ttyRPMSG> menu\n"
            "\n"
            "  One-shot CLI:\n"
            "    %s <ttyRPMSG> ping\n"
            "    %s <ttyRPMSG> store-aes <32-hex>\n"
            "    %s <ttyRPMSG> export-aes <file>\n"
            "    %s <ttyRPMSG> load-aes <file>\n"
            "    %s <ttyRPMSG> encrypt-gcm <24-hex-iv> <aad-string> <pt-string>\n"
            "    %s <ttyRPMSG> decrypt-gcm <24-hex-iv> <aad-string> <ct+tag-hex>\n"
            "    %s <ttyRPMSG> store-hmac <hex-key>\n"
            "    %s <ttyRPMSG> export-hmac <file>\n"
            "    %s <ttyRPMSG> load-hmac <file>\n"
            "    %s <ttyRPMSG> sign <data-string>\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

static int run_cli(int argc, char **argv)
{
    int fd;
    uint8_t buf[480];
    uint8_t out[480];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    const char *dev  = argv[1];
    const char *cmd  = argv[2];

    fd = open_rpmsg(dev);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    if (!strcmp(cmd, "ping"))
    {
        if (transact(fd, M7CR_CMD_PING, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        printf("payload=");
        fwrite(out, 1, out_len, stdout);
        printf("\n");
    }
    else if (!strcmp(cmd, "store-aes") && argc == 4)
    {
        size_t n = 0;
        if (parse_hex(argv[3], buf, sizeof(buf), &n) || n != M7CR_AES_KEY_LEN)
        {
            fprintf(stderr, "need 16-byte AES key as 32 hex chars\n");
            close(fd);
            return 1;
        }
        if (transact(fd, M7CR_CMD_STORE_AES_KEY, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "export-aes") && argc == 4)
    {
        FILE *f;
        if (transact(fd, M7CR_CMD_EXPORT_AES_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        if (status == M7CR_OK)
        {
            f = fopen(argv[3], "wb");
            if (!f)
            {
                perror("fopen");
                close(fd);
                return 1;
            }
            fwrite(out, 1, out_len, f);
            fclose(f);
        }
    }
    else if (!strcmp(cmd, "load-aes") && argc == 4)
    {
        FILE *f = fopen(argv[3], "rb");
        size_t n;
        if (!f)
        {
            perror("fopen");
            close(fd);
            return 1;
        }
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (transact(fd, M7CR_CMD_LOAD_AES_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
    }
    else if (!strcmp(cmd, "encrypt-gcm") && argc == 6)
    {
        size_t ivn           = 0;
        const char *aad      = argv[4];
        const char *pt       = argv[5];
        uint16_t aad_len     = (uint16_t)strlen(aad);
        uint16_t pt_len      = (uint16_t)strlen(pt);
        uint32_t plen;
        if (parse_hex(argv[3], buf, 12, &ivn) || ivn != 12)
        {
            fprintf(stderr, "IV must be 12 bytes (24 hex chars)\n");
            close(fd);
            return 1;
        }
        plen = 12 + 2 + 2 + aad_len + pt_len;
        if (plen > sizeof(buf))
        {
            fprintf(stderr, "payload too large for RPMsg buffer\n");
            close(fd);
            return 1;
        }
        buf[12] = (uint8_t)(aad_len & 0xff);
        buf[13] = (uint8_t)(aad_len >> 8);
        buf[14] = (uint8_t)(pt_len & 0xff);
        buf[15] = (uint8_t)(pt_len >> 8);
        memcpy(buf + 16, aad, aad_len);
        memcpy(buf + 16 + aad_len, pt, pt_len);
        if (transact(fd, M7CR_CMD_ENCRYPT_GCM, buf, plen, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        printf("ct_tag=");
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "decrypt-gcm") && argc == 6)
    {
        size_t ivn = 0, ctn = 0;
        const char *aad  = argv[4];
        uint16_t aad_len = (uint16_t)strlen(aad);
        uint8_t ctbuf[400];
        uint16_t ct_len;
        uint32_t plen;
        if (parse_hex(argv[3], buf, 12, &ivn) || ivn != 12)
        {
            fprintf(stderr, "IV must be 12 bytes\n");
            close(fd);
            return 1;
        }
        if (parse_hex(argv[5], ctbuf, sizeof(ctbuf), &ctn) || ctn < M7CR_GCM_TAG_LEN)
        {
            fprintf(stderr, "ct+tag hex invalid\n");
            close(fd);
            return 1;
        }
        ct_len = (uint16_t)(ctn - M7CR_GCM_TAG_LEN);
        plen   = 12 + 2 + 2 + aad_len + ctn;
        if (plen > sizeof(buf))
        {
            fprintf(stderr, "payload too large\n");
            close(fd);
            return 1;
        }
        buf[12] = (uint8_t)(aad_len & 0xff);
        buf[13] = (uint8_t)(aad_len >> 8);
        buf[14] = (uint8_t)(ct_len & 0xff);
        buf[15] = (uint8_t)(ct_len >> 8);
        memcpy(buf + 16, aad, aad_len);
        memcpy(buf + 16 + aad_len, ctbuf, ctn);
        if (transact(fd, M7CR_CMD_DECRYPT_GCM, buf, plen, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        printf("pt=");
        fwrite(out, 1, out_len, stdout);
        printf("\n");
    }
    else if (!strcmp(cmd, "store-hmac") && argc == 4)
    {
        size_t n = 0;
        if (parse_hex(argv[3], buf + 2, sizeof(buf) - 2, &n) || n == 0 || n > M7CR_HMAC_KEY_MAX)
        {
            fprintf(stderr, "invalid hmac key hex\n");
            close(fd);
            return 1;
        }
        buf[0] = (uint8_t)(n & 0xff);
        buf[1] = (uint8_t)(n >> 8);
        if (transact(fd, M7CR_CMD_STORE_HMAC_KEY, buf, (uint32_t)(2 + n), out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "export-hmac") && argc == 4)
    {
        FILE *f;
        if (transact(fd, M7CR_CMD_EXPORT_HMAC_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        if (status == M7CR_OK)
        {
            f = fopen(argv[3], "wb");
            if (!f)
            {
                perror("fopen");
                close(fd);
                return 1;
            }
            fwrite(out, 1, out_len, f);
            fclose(f);
        }
    }
    else if (!strcmp(cmd, "load-hmac") && argc == 4)
    {
        FILE *f = fopen(argv[3], "rb");
        size_t n;
        if (!f)
        {
            perror("fopen");
            close(fd);
            return 1;
        }
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (transact(fd, M7CR_CMD_LOAD_HMAC_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        print_status(status, out_len);
    }
    else if (!strcmp(cmd, "sign") && argc == 4)
    {
        size_t n = strlen(argv[3]);
        if (transact(fd, M7CR_CMD_SIGN_HMAC, (const uint8_t *)argv[3], (uint32_t)n, out, sizeof(out), &out_len,
                     &status) != 0)
            goto ioerr;
        print_status(status, out_len);
        printf("mac=");
        print_hex(out, out_len);
    }
    else
    {
        usage(argv[0]);
        close(fd);
        return 1;
    }

    close(fd);
    return status == M7CR_OK ? 0 : 2;

ioerr:
    perror("I/O or protocol error");
    close(fd);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 1)
        return menu_loop();

    if (argc == 2)
    {
        strncpy(g_dev, argv[1], sizeof(g_dev) - 1u);
        g_dev[sizeof(g_dev) - 1u] = '\0';
        return menu_loop();
    }

    if (argc == 3 && !strcmp(argv[2], "menu"))
    {
        strncpy(g_dev, argv[1], sizeof(g_dev) - 1u);
        g_dev[sizeof(g_dev) - 1u] = '\0';
        return menu_loop();
    }

    if (argc >= 3)
        return run_cli(argc, argv);

    usage(argv[0]);
    return 1;
}
