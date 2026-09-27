/*
 * Shared Linux <-> M7 crypto RPMsg protocol.
 * Little-endian. One request/response per RPMsg (or ttyRPMSG) message.
 *
 * Payload room is limited by RL_BUFFER_PAYLOAD_SIZE (496 on EVK pingpong).
 * Keep plaintext/ciphertext + AAD within ~400 bytes per message.
 */
#ifndef M7_CRYPTO_PROTOCOL_H_
#define M7_CRYPTO_PROTOCOL_H_

#include <stdint.h>

#define M7CR_MAGIC              (0x4D374352u) /* 'M7CR' */
#define M7CR_VERSION            (1u)

#define M7CR_AES_KEY_LEN        (16u)
#define M7CR_GCM_IV_LEN         (12u)
#define M7CR_GCM_TAG_LEN        (16u)
#define M7CR_HMAC_LEN           (32u)
#define M7CR_HMAC_KEY_MAX       (64u)

/* Soft blob: temporary wrap until CAAM black blob is wired in. */
#define M7CR_BLOB_MAGIC         (0x424C4F42u) /* 'BLOB' */
#define M7CR_BLOB_VERSION       (1u)
#define M7CR_BLOB_TYPE_AES128   (1u)
#define M7CR_BLOB_TYPE_HMAC     (2u)

enum m7cr_cmd
{
    M7CR_CMD_STORE_AES_KEY   = 1,  /* in: 16B key; out: soft blob */
    M7CR_CMD_LOAD_AES_BLOB   = 2,  /* in: soft blob; out: empty */
    M7CR_CMD_EXPORT_AES_BLOB = 3,  /* in: empty; out: soft blob */
    M7CR_CMD_ENCRYPT_GCM     = 4,  /* in: gcm_req; out: ct||tag */
    M7CR_CMD_DECRYPT_GCM     = 5,  /* in: gcm_dec; out: pt */
    M7CR_CMD_STORE_HMAC_KEY  = 6,  /* in: u16 key_len + key; out: soft blob */
    M7CR_CMD_LOAD_HMAC_BLOB  = 7,  /* in: soft blob; out: empty */
    M7CR_CMD_EXPORT_HMAC_BLOB= 8,  /* in: empty; out: soft blob */
    M7CR_CMD_SIGN_HMAC       = 9,  /* in: data; out: 32B mac */
    M7CR_CMD_PING            = 10, /* in: empty; out: "OK" */
};

enum m7cr_status
{
    M7CR_OK              = 0,
    M7CR_ERR_MAGIC       = 1,
    M7CR_ERR_VERSION     = 2,
    M7CR_ERR_CMD         = 3,
    M7CR_ERR_LENGTH      = 4,
    M7CR_ERR_NO_KEY      = 5,
    M7CR_ERR_CRYPTO      = 6,
    M7CR_ERR_AUTH        = 7,
    M7CR_ERR_BLOB        = 8,
    M7CR_ERR_BUSY        = 9,
    M7CR_ERR_INTERNAL    = 10,
};

#pragma pack(push, 1)
typedef struct m7cr_req_hdr
{
    uint32_t magic;
    uint16_t version;
    uint16_t cmd;
    uint32_t req_id;
    uint32_t length; /* payload bytes after header */
} m7cr_req_hdr_t;

typedef struct m7cr_rsp_hdr
{
    uint32_t magic;
    uint16_t version;
    uint16_t status;
    uint32_t req_id;
    uint32_t length;
} m7cr_rsp_hdr_t;

/* ENCRYPT_GCM payload after header:
 *   uint8_t  iv[12]
 *   uint16_t aad_len
 *   uint16_t pt_len
 *   uint8_t  aad[aad_len]
 *   uint8_t  pt[pt_len]
 * Response: ct[pt_len] || tag[16]
 */

/* DECRYPT_GCM payload after header:
 *   uint8_t  iv[12]
 *   uint16_t aad_len
 *   uint16_t ct_len
 *   uint8_t  aad[aad_len]
 *   uint8_t  ct[ct_len]
 *   uint8_t  tag[16]
 * Response: pt[ct_len]
 */

typedef struct m7cr_blob_hdr
{
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint16_t key_len;
    uint16_t reserved;
    uint8_t  wrap_iv[16];
    /* followed by: wrapped_key[key_len rounded to 16] || mac[32] */
} m7cr_blob_hdr_t;
#pragma pack(pop)

#endif /* M7_CRYPTO_PROTOCOL_H_ */
