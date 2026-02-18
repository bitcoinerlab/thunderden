#ifndef THUNDERDEN_PSBT_PAYLOAD_DECODER_H
#define THUNDERDEN_PSBT_PAYLOAD_DECODER_H

#include <stddef.h>
#include <stdint.h>

struct psbt_payload_decoder;

struct psbt_payload_decoder *psbt_payload_decoder_create(void);
void psbt_payload_decoder_destroy(struct psbt_payload_decoder *decoder);

/*
 * Consume one QR payload fragment.
 *
 * Returns:
 *   1 when a full PSBT was decoded and normalized to base64 in out/out_len.
 *   0 when fragment was ignored or more fragments are needed.
 */
int psbt_payload_decoder_consume(struct psbt_payload_decoder *decoder,
                                const uint8_t *payload,
                                size_t payload_len,
                                char *out,
                                size_t out_len);

#endif
