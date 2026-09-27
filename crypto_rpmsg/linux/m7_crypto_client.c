/* Userspace client for the M7 crypto service on /dev/ttyRPMSG30. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../m7/m7_crypto_protocol.h"

static uint32_t g_req_id = 1;

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

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s <ttyRPMSG> ping\n"
            "  %s <ttyRPMSG> store-aes <32-hex>\n"
            "  %s <ttyRPMSG> export-aes <file>\n"
            "  %s <ttyRPMSG> load-aes <file>\n"
            "  %s <ttyRPMSG> encrypt-gcm <24-hex-iv> <aad-string> <pt-string>\n"
            "  %s <ttyRPMSG> decrypt-gcm <24-hex-iv> <aad-string> <ct+tag-hex>\n"
            "  %s <ttyRPMSG> store-hmac <hex-key>\n"
            "  %s <ttyRPMSG> export-hmac <file>\n"
            "  %s <ttyRPMSG> load-hmac <file>\n"
            "  %s <ttyRPMSG> sign <data-string>\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
    int fd;
    uint8_t buf[480];
    uint8_t out[480];
    uint32_t out_len = 0;
    uint16_t status  = 0xffff;
    const char *dev;
    const char *cmd;

    if (argc < 3)
    {
        usage(argv[0]);
        return 1;
    }
    dev = argv[1];
    cmd = argv[2];

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
        printf("status=%u payload=", status);
        fwrite(out, 1, out_len, stdout);
        printf("\n");
    }
    else if (!strcmp(cmd, "store-aes") && argc == 4)
    {
        size_t n = 0;
        if (parse_hex(argv[3], buf, sizeof(buf), &n) || n != M7CR_AES_KEY_LEN)
        {
            fprintf(stderr, "need 16-byte AES key as 32 hex chars\n");
            return 1;
        }
        if (transact(fd, M7CR_CMD_STORE_AES_KEY, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u blob_len=%u\n", status, out_len);
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "export-aes") && argc == 4)
    {
        FILE *f;
        if (transact(fd, M7CR_CMD_EXPORT_AES_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u\n", status);
        if (status == M7CR_OK)
        {
            f = fopen(argv[3], "wb");
            if (!f)
            {
                perror("fopen");
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
            return 1;
        }
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (transact(fd, M7CR_CMD_LOAD_AES_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u\n", status);
    }
    else if (!strcmp(cmd, "encrypt-gcm") && argc == 6)
    {
        size_t ivn = 0;
        const char *aad = argv[4];
        const char *pt  = argv[5];
        uint16_t aad_len = (uint16_t)strlen(aad);
        uint16_t pt_len  = (uint16_t)strlen(pt);
        uint32_t plen;
        if (parse_hex(argv[3], buf, 12, &ivn) || ivn != 12)
        {
            fprintf(stderr, "IV must be 12 bytes (24 hex chars)\n");
            return 1;
        }
        plen = 12 + 2 + 2 + aad_len + pt_len;
        if (plen > sizeof(buf))
        {
            fprintf(stderr, "payload too large for RPMsg buffer\n");
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
        printf("status=%u ct_tag=", status);
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "decrypt-gcm") && argc == 6)
    {
        size_t ivn = 0, ctn = 0;
        const char *aad = argv[4];
        uint16_t aad_len = (uint16_t)strlen(aad);
        uint8_t ctbuf[400];
        uint16_t ct_len;
        uint32_t plen;
        if (parse_hex(argv[3], buf, 12, &ivn) || ivn != 12)
        {
            fprintf(stderr, "IV must be 12 bytes\n");
            return 1;
        }
        if (parse_hex(argv[5], ctbuf, sizeof(ctbuf), &ctn) || ctn < M7CR_GCM_TAG_LEN)
        {
            fprintf(stderr, "ct+tag hex invalid\n");
            return 1;
        }
        ct_len = (uint16_t)(ctn - M7CR_GCM_TAG_LEN);
        plen   = 12 + 2 + 2 + aad_len + ctn;
        if (plen > sizeof(buf))
        {
            fprintf(stderr, "payload too large\n");
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
        printf("status=%u pt=", status);
        fwrite(out, 1, out_len, stdout);
        printf("\n");
    }
    else if (!strcmp(cmd, "store-hmac") && argc == 4)
    {
        size_t n = 0;
        if (parse_hex(argv[3], buf + 2, sizeof(buf) - 2, &n) || n == 0 || n > M7CR_HMAC_KEY_MAX)
        {
            fprintf(stderr, "invalid hmac key hex\n");
            return 1;
        }
        buf[0] = (uint8_t)(n & 0xff);
        buf[1] = (uint8_t)(n >> 8);
        if (transact(fd, M7CR_CMD_STORE_HMAC_KEY, buf, (uint32_t)(2 + n), out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u blob_len=%u\n", status, out_len);
        print_hex(out, out_len);
    }
    else if (!strcmp(cmd, "export-hmac") && argc == 4)
    {
        FILE *f;
        if (transact(fd, M7CR_CMD_EXPORT_HMAC_BLOB, NULL, 0, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u\n", status);
        if (status == M7CR_OK)
        {
            f = fopen(argv[3], "wb");
            if (!f)
            {
                perror("fopen");
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
            return 1;
        }
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (transact(fd, M7CR_CMD_LOAD_HMAC_BLOB, buf, (uint32_t)n, out, sizeof(out), &out_len, &status) != 0)
            goto ioerr;
        printf("status=%u\n", status);
    }
    else if (!strcmp(cmd, "sign") && argc == 4)
    {
        size_t n = strlen(argv[3]);
        if (transact(fd, M7CR_CMD_SIGN_HMAC, (const uint8_t *)argv[3], (uint32_t)n, out, sizeof(out), &out_len,
                     &status) != 0)
            goto ioerr;
        printf("status=%u mac=", status);
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
