/*
 *  This file is part of the Off-the-Record Next Generation Messaging
 *  library (libotr-ng).
 *
 *  Copyright (C) 2016-2018, the libotr-ng contributors.
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "prekey_client.h"

#include "dake.h"
#include "deserialize.h"
#include "fingerprint.h"
#include "random.h"
#include "serialize.h"
#include "shake.h"

#include <libotr/b64.h>
#include <libotr/mem.h>

#define OTRNG_PREKEY_DAKE1_MSG 0x35
#define OTRNG_PREKEY_DAKE2_MSG 0x36
#define OTRNG_PREKEY_DAKE3_MSG 0x37
#define OTRNG_PREKEY_STORAGE_INFO_REQ_MSG 0x09
#define OTRNG_PREKEY_STORAGE_STATUS_MSG 0x0B
#define OTRNG_PREKEY_PUBLICATION_MSG 0x08

API otrng_prekey_client_s *
otrng_prekey_client_new(const char *server, const char *our_identity,
                        uint32_t instance_tag, const otrng_keypair_s *keypair,
                        const client_profile_s *client_profile,
                        const otrng_prekey_profile_s *prekey_profile) {
  if (!server) {
    return NULL;
  }

  if (!our_identity) {
    return NULL;
  }

  if (!instance_tag) {
    return NULL;
  }

  if (!client_profile) {
    return NULL;
  }

  otrng_prekey_client_s *ret = malloc(sizeof(otrng_prekey_client_s));
  if (!ret) {
    return NULL;
  }

  ret->instance_tag = instance_tag;
  ret->client_profile = client_profile;
  // TODO: Can be null if you dont want to publish it
  ret->prekey_profile = prekey_profile;
  ret->keypair = keypair;
  ret->server_identity = otrng_strdup(server);
  ret->our_identity = otrng_strdup(our_identity);
  otrng_ecdh_keypair_destroy(ret->ephemeral_ecdh);

  return ret;
}

API void otrng_prekey_client_free(otrng_prekey_client_s *client) {
  if (!client) {
    return;
  }

  otrng_ecdh_keypair_destroy(client->ephemeral_ecdh);
  client->client_profile = NULL;

  free(client->server_identity);
  client->server_identity = NULL;

  free(client->our_identity);
  client->our_identity = NULL;
}

static otrng_err prekey_decode(const char *message, uint8_t **buffer,
                               size_t *buffer_len) {
  size_t l = strlen(message);

  if (!l || '.' != message[l - 1]) {
    return OTRNG_ERROR;
  }

  *buffer = malloc(((l - 1 + 3) / 4) * 3);
  if (!*buffer) {
    return OTRNG_ERROR;
  }

  *buffer_len = otrl_base64_decode(*buffer, message, l - 1);
  return OTRNG_SUCCESS;
}

static char *prekey_encode(const uint8_t *buffer, size_t buffer_len) {
  size_t base64_len = ((buffer_len + 2) / 3) * 4;
  char *ret = malloc(base64_len + 2);
  if (!ret) {
    return NULL;
  }

  size_t l = otrl_base64_encode(ret, buffer, buffer_len);
  ret[l] = '.';
  ret[l + 1] = 0;

  return ret;
}

static char *start_dake_and_then_send(otrng_prekey_client_s *client,
                                      otrng_prekey_next_message_t next) {
  otrng_prekey_dake1_message_s msg[1];
  msg->client_instance_tag = client->instance_tag;
  otrng_client_profile_copy(msg->client_profile, client->client_profile);

  uint8_t sym[ED448_PRIVATE_BYTES] = {0};
  random_bytes(sym, ED448_PRIVATE_BYTES);
  otrng_ecdh_keypair_generate(client->ephemeral_ecdh, sym);
  goldilocks_bzero(sym, ED448_PRIVATE_BYTES);
  otrng_ec_point_copy(msg->I, client->ephemeral_ecdh->pub);

  uint8_t *serialized = NULL;
  size_t serialized_len = 0;
  otrng_err success =
      otrng_prekey_dake1_message_asprint(&serialized, &serialized_len, msg);
  otrng_prekey_dake1_message_destroy(msg);

  if (!success) {
    return NULL;
  }

  char *ret = prekey_encode(serialized, serialized_len);
  free(serialized);

  client->after_dake = next;
  return ret;
}

// TODO: rename
// this sends a "Storage Information Request" but it is used to "request storage
// information"(?)
API char *
otrng_prekey_client_request_storage_status(otrng_prekey_client_s *client) {
  return start_dake_and_then_send(client,
                                  OTRNG_PREKEY_STORAGE_INFORMATION_REQUEST);
}

// TODO: this can publish up to 255 prekeys. How will this be handled? via
// callback? Via parameter?
API char *otrng_prekey_client_publish_prekeys(otrng_prekey_client_s *client) {
  return start_dake_and_then_send(client, OTRNG_PREKEY_PREKEY_PUBLICATION);
}

// What if we want to publish ONLY the profiles?
// API char *
// otrng_prekey_client_publish_profiles(otrng_prekey_client_s *client) {
//}

// instance tag, jid,
API char *otrng_prekey_client_retrieve_prekeys(const char *identity,
                                               const char *versions,
                                               otrng_prekey_client_s *client) {
  otrng_prekey_ensemble_query_retrieval_message_s msg[1];

  msg->identity = otrng_strdup(identity);
  msg->versions = otrng_strdup(versions);
  msg->instance_tag = client->instance_tag;

  uint8_t *serialized = NULL;
  size_t serialized_len = 0;
  otrng_err success = otrng_prekey_ensemble_query_retrieval_message_asprint(
      &serialized, &serialized_len, msg);

  otrng_prekey_ensemble_query_retrieval_message_destroy(msg);

  if (!success) {
    return NULL;
  }

  char *ret = prekey_encode(serialized, serialized_len);
  free(serialized);
  return ret;
}

INTERNAL otrng_err otrng_prekey_ensemble_query_retrieval_message_asprint(
    uint8_t **dst, size_t *len,
    const otrng_prekey_ensemble_query_retrieval_message_s *msg) {
  if (!len || !dst) {
    return OTRNG_ERROR;
  }

  *len = 2 + 1 + 4 + (4 + strlen(msg->identity)) + (4 + strlen(msg->versions));
  *dst = malloc(*len);
  if (!*dst) {
    return OTRNG_ERROR;
  }

  size_t w = 0;
  w += otrng_serialize_uint16(*dst, 0x04);
  w += otrng_serialize_uint8(*dst + w, 0x10);
  w += otrng_serialize_uint32(*dst + w, msg->instance_tag);
  w += otrng_serialize_data(*dst + w, (uint8_t *)msg->identity,
                            strlen(msg->identity));
  w += otrng_serialize_data(*dst + w, (uint8_t *)msg->versions,
                            strlen(msg->versions));

  return OTRNG_SUCCESS;
}

INTERNAL void otrng_prekey_ensemble_query_retrieval_message_destroy(
    otrng_prekey_ensemble_query_retrieval_message_s *msg) {
  if (!msg) {
    return;
  }

  free(msg->identity);
  msg->identity = NULL;

  free(msg->versions);
  msg->versions = NULL;
}

static uint8_t *otrng_prekey_client_get_expected_composite_phi(
    size_t *len, const otrng_prekey_client_s *client) {
  if (!client->server_identity || !client->our_identity) {
    return NULL;
  }

  size_t s =
      4 + strlen(client->server_identity) + 4 + strlen(client->our_identity);
  uint8_t *dst = malloc(s);
  if (!dst) {
    return NULL;
  }

  size_t w = 0;
  w += otrng_serialize_data(dst + w, (const uint8_t *)client->our_identity,
                            strlen(client->our_identity));
  w += otrng_serialize_data(dst + w, (const uint8_t *)client->server_identity,
                            strlen(client->server_identity));

  if (len) {
    *len = s;
  }

  return dst;
}

INTERNAL void kdf_init_with_usage(goldilocks_shake256_ctx_p hash,
                                  uint8_t usage) {
  hash_init_with_usage_and_domain_separation(hash, usage, "OTR-Prekey-Server");
}

static otrng_bool
otrng_prekey_dake2_message_valid(const otrng_prekey_dake2_message_s *msg,
                                 const otrng_prekey_client_s *client) {
  // The spec says:
  // "Ensure the identity element of the Prekey Server Composite Identity is
  // correct." We make this check implicitly by verifying the ring signature
  // (which contains this value as part of its "composite identity".

  // TODO: Check if the fingerprint from the key received in this message is
  // what we expect. Through a callback maybe, since the user may need to take
  // action.

  size_t composite_phi_len = 0;
  uint8_t *composite_phi = otrng_prekey_client_get_expected_composite_phi(
      &composite_phi_len, client);

  uint8_t *our_profile = NULL;
  size_t our_profile_len = 0;
  if (!otrng_client_profile_asprintf(&our_profile, &our_profile_len,
                                     client->client_profile)) {
    return otrng_false;
  }

  size_t tlen = 1 + 3 * 64 + 2 * ED448_POINT_BYTES;
  uint8_t *t = malloc(tlen);
  if (!t) {
    free(our_profile);
    return otrng_false;
  }

  *t = 0x0;
  size_t w = 1;

  goldilocks_shake256_ctx_p h1;
  kdf_init_with_usage(h1, 0x02);
  hash_update(h1, our_profile, our_profile_len);
  hash_final(h1, t + w, 64);
  hash_destroy(h1);
  free(our_profile);

  w += 64;

  // Both composite identity AND composite phi have the server's bare JID
  goldilocks_shake256_ctx_p h2;
  kdf_init_with_usage(h2, 0x03);
  hash_update(h2, msg->composite_identity, msg->composite_identity_len);
  hash_final(h2, t + w, 64);
  hash_destroy(h2);

  w += 64;

  w += otrng_serialize_ec_point(t + w, client->ephemeral_ecdh->pub);
  w += otrng_serialize_ec_point(t + w, msg->S);

  goldilocks_shake256_ctx_p h3;
  kdf_init_with_usage(h3, 0x04);
  hash_update(h3, composite_phi, composite_phi_len);
  hash_final(h3, t + w, 64);
  hash_destroy(h3);
  free(composite_phi);

  otrng_bool ret = otrng_rsig_verify_with_usage_and_domain(
      0x11, "OTR-Prekey-Server", msg->sigma, client->keypair->pub,
      msg->server_pub_key, client->ephemeral_ecdh->pub, t, tlen);
  free(t);

  return ret;
}

INTERNAL otrng_err
otrng_prekey_dake3_message_append_storage_information_request(
    otrng_prekey_dake3_message_s *msg, uint8_t mac_key[64]) {
  msg->message = malloc(2 + 1 + 64);
  msg->message_len = 67;
  if (!msg->message) {
    return OTRNG_ERROR;
  }

  uint8_t msg_type = OTRNG_PREKEY_STORAGE_INFO_REQ_MSG;
  size_t w = 0;
  w += otrng_serialize_uint16(msg->message, OTRNG_PROTOCOL_VERSION_4);
  w += otrng_serialize_uint8(msg->message + w, msg_type);

  // MAC: KDF(usage_storage_info_MAC, prekey_mac_k || message type, 64)
  goldilocks_shake256_ctx_p hmac;
  kdf_init_with_usage(hmac, 0x0A);
  hash_update(hmac, mac_key, 64);
  hash_update(hmac, &msg_type, 1);
  hash_final(hmac, msg->message + w, 64);
  hash_destroy(hmac);

  return OTRNG_SUCCESS;
}

INTERNAL otrng_err otrng_prekey_dake3_message_append_prekey_publication_message(
    otrng_prekey_publication_message_s *pub_msg,
    otrng_prekey_dake3_message_s *msg, uint8_t mac_key[64]) {

  uint8_t *client_profile = NULL;
  size_t client_profile_len = 0;
  if (!otrng_client_profile_asprintf(&client_profile, &client_profile_len,
                                     pub_msg->client_profile)) {
    return OTRNG_ERROR;
  }

  uint8_t *prekey_profile = NULL;
  size_t prekey_profile_len = 0;
  if (!otrng_prekey_profile_asprint(&prekey_profile, &prekey_profile_len,
                                    pub_msg->prekey_profile)) {
    free(client_profile);
    return OTRNG_ERROR;
  }

  size_t s = 2 + 1 + 1 +
             (4 + pub_msg->num_prekey_messages * PRE_KEY_MAX_BYTES) + 1 +
             client_profile_len + 1 + prekey_profile_len + 64;
  msg->message = malloc(s);
  if (!msg->message) {
    free(client_profile);
    free(prekey_profile);
    return OTRNG_ERROR;
  }

  uint8_t msg_type = OTRNG_PREKEY_PUBLICATION_MSG;
  size_t w = 0;
  w += otrng_serialize_uint16(msg->message, OTRNG_PROTOCOL_VERSION_4);
  w += otrng_serialize_uint8(msg->message + w, msg_type);

  w += otrng_serialize_uint8(msg->message + w, pub_msg->num_prekey_messages);

  const uint8_t *prekey_messages_beginning = msg->message + w;
  for (int i = 0; i < pub_msg->num_prekey_messages; i++) {
    size_t w2 = 0;
    if (!otrng_dake_prekey_message_serialize(msg->message + w, s - w, &w2,
                                             pub_msg->prekey_messages[i])) {
      free(client_profile);
      free(prekey_profile);
      return OTRNG_ERROR;
    }
    w += w2;
  }

  // TODO: the spec also implies that either you have ONLY prekey messages OR
  // you have prekey messages AND both profiles (see how the mac is explained at
  // the spec). So J and K can only be both 1 or both 0, and I don't know why
  // there is J and K as separate variables.

  // The MAC could be a KDF over the entire message, but this "conditional
  // nested KDF" structure makes it uneccessarily complicated.
  uint8_t prekey_messages_kdf[64] = {0};
  goldilocks_shake256_ctx_p hmac;
  kdf_init_with_usage(hmac, 0x0E);
  hash_update(hmac, prekey_messages_beginning,
              msg->message + w - prekey_messages_beginning);
  hash_final(hmac, prekey_messages_kdf, 64);
  hash_destroy(hmac);

  w += otrng_serialize_uint8(msg->message + w, pub_msg->client_profile ? 1 : 0);
  w += otrng_serialize_bytes_array(msg->message + w, client_profile,
                                   client_profile_len);

  w += otrng_serialize_uint8(msg->message + w, pub_msg->prekey_profile ? 1 : 0);
  w += otrng_serialize_bytes_array(msg->message + w, prekey_profile,
                                   prekey_profile_len);

  // MAC: KDF(usage_preMAC, prekey_mac_k || message type
  //          || N || KDF(usage_prekey_message, Prekey Messages, 64)
  //          || K || KDF(usage_client_profile, Client Profile, 64)
  //          || J || KDF(usage_prekey_profile, Prekey Profile, 64),
  //      64)

  uint8_t client_profile_kdf[64] = {0};
  if (pub_msg->client_profile) {
    kdf_init_with_usage(hmac, 0x0F);
    hash_update(hmac, client_profile, client_profile_len);
    hash_final(hmac, client_profile_kdf, 64);
    hash_destroy(hmac);
  }

  uint8_t prekey_profile_kdf[64] = {0};
  if (pub_msg->prekey_profile) {
    kdf_init_with_usage(hmac, 0x10);
    hash_update(hmac, prekey_profile, prekey_profile_len);
    hash_final(hmac, prekey_profile_kdf, 64);
    hash_destroy(hmac);
  }

  uint8_t one = 1, zero = 0;
  kdf_init_with_usage(hmac, 0x09);
  hash_update(hmac, mac_key, 64);
  hash_update(hmac, &msg_type, 1);
  hash_update(hmac, &pub_msg->num_prekey_messages, 1);
  hash_update(hmac, prekey_messages_kdf, 64);

  if (pub_msg->client_profile) {
    hash_update(hmac, &one, 1);
    hash_update(hmac, client_profile_kdf, 64);
  } else {
    hash_update(hmac, &zero, 1);
  }

  if (pub_msg->prekey_profile) {
    hash_update(hmac, &one, 1);
    hash_update(hmac, prekey_profile_kdf, 64);
  } else {
    hash_update(hmac, &zero, 1);
  }
  hash_final(hmac, msg->message + w, 64);
  hash_destroy(hmac);

  msg->message_len = w + 64;

  return OTRNG_SUCCESS;
}

static char *send_dake3(const otrng_prekey_dake2_message_s *msg2,
                        otrng_prekey_client_s *client) {
  otrng_prekey_dake3_message_s msg[1];

  msg->client_instance_tag = client->instance_tag;

  size_t composite_phi_len = 0;
  uint8_t *composite_phi = otrng_prekey_client_get_expected_composite_phi(
      &composite_phi_len, client);

  uint8_t *our_profile = NULL;
  size_t our_profile_len = 0;
  if (!otrng_client_profile_asprintf(&our_profile, &our_profile_len,
                                     client->client_profile)) {
    return NULL;
  }

  size_t tlen = 1 + 3 * 64 + 2 * ED448_POINT_BYTES;
  uint8_t *t = malloc(tlen);
  if (!t) {
    free(our_profile);
    return NULL;
  }

  *t = 0x1;
  size_t w = 1;

  goldilocks_shake256_ctx_p h1;
  kdf_init_with_usage(h1, 0x05);
  hash_update(h1, our_profile, our_profile_len);
  hash_final(h1, t + w, 64);
  hash_destroy(h1);
  free(our_profile);

  w += 64;

  // Both composite identity AND composite phi have the server's bare JID
  goldilocks_shake256_ctx_p h2;
  kdf_init_with_usage(h2, 0x06);
  hash_update(h2, msg2->composite_identity, msg2->composite_identity_len);
  hash_final(h2, t + w, 64);
  hash_destroy(h2);

  w += 64;

  w += otrng_serialize_ec_point(t + w, client->ephemeral_ecdh->pub);
  w += otrng_serialize_ec_point(t + w, msg2->S);

  goldilocks_shake256_ctx_p h3;
  kdf_init_with_usage(h3, 0x07);
  hash_update(h3, composite_phi, composite_phi_len);
  hash_final(h3, t + w, 64);
  hash_destroy(h3);
  free(composite_phi);

  // H_a, sk_ha, {H_a, H_s, S}, t
  otrng_rsig_authenticate_with_usage_and_domain(
      0x11, "OTR-Prekey-Server", msg->sigma, client->keypair->priv,
      client->keypair->pub, client->keypair->pub, msg2->server_pub_key, msg2->S,
      t, tlen);
  free(t);

  // ECDH(i, S)
  uint8_t shared_secret[64] = {0};
  uint8_t ecdh_shared[ED448_POINT_BYTES] = {0};
  otrng_ecdh_shared_secret(ecdh_shared, sizeof(ecdh_shared),
                           client->ephemeral_ecdh->priv, msg2->S);

  // SK = KDF(0x01, ECDH(i, S), 64)
  goldilocks_shake256_ctx_p hsk;
  kdf_init_with_usage(hsk, 0x01);
  hash_update(hsk, ecdh_shared, ED448_POINT_BYTES);
  hash_final(hsk, shared_secret, 64);
  hash_destroy(hsk);

  // prekey_mac_k = KDF(0x08, SK, 64)
  goldilocks_shake256_ctx_p hpk;
  kdf_init_with_usage(hpk, 0x08);
  hash_update(hpk, shared_secret, 64);
  hash_final(hpk, client->mac_key, 64);
  hash_destroy(hpk);

  // Put the MESSAGE in the message
  if (client->after_dake == OTRNG_PREKEY_STORAGE_INFORMATION_REQUEST) {
    if (!otrng_prekey_dake3_message_append_storage_information_request(
            msg, client->mac_key)) {
      return NULL;
    }
  } else if (client->after_dake == OTRNG_PREKEY_PREKEY_PUBLICATION) {
    otrng_prekey_publication_message_s pub_msg[1];

    // TODO: They need to be stored somewhere, so it will probably be
    // a callback. This way the plugin can decide where to store this.
    ecdh_keypair_p ecdh;
    dh_keypair_p dh;
    otrng_generate_ephemeral_keys(ecdh, dh);

    // Create a single prekey message
    dake_prekey_message_s *prekey_msg = otrng_dake_prekey_message_build(
        client->instance_tag, ecdh->pub, dh->pub);
    if (!prekey_msg) {
      return NULL; // error
    }

    // TODO: We create a sample publication message
    // We may invoke a callback that knows what should be put here
    pub_msg->num_prekey_messages = 1;
    pub_msg->prekey_messages = malloc(sizeof(dake_prekey_message_s *));
    pub_msg->prekey_messages[0] = prekey_msg;
    pub_msg->client_profile = malloc(sizeof(client_profile_s));
    otrng_client_profile_copy(pub_msg->client_profile, client->client_profile);
    pub_msg->prekey_profile = malloc(sizeof(otrng_prekey_profile_s));
    otrng_prekey_profile_copy(pub_msg->prekey_profile, client->prekey_profile);

    otrng_err success =
        otrng_prekey_dake3_message_append_prekey_publication_message(
            pub_msg, msg, client->mac_key);
    otrng_prekey_publication_message_destroy(pub_msg);

    if (!success) {
      return NULL;
    }
  } else {
    return NULL;
  }

  client->after_dake = 0;

  uint8_t *serialized = NULL;
  size_t serialized_len = 0;
  otrng_err success =
      otrng_prekey_dake3_message_asprint(&serialized, &serialized_len, msg);
  otrng_prekey_dake3_message_destroy(msg);

  if (!success) {
    return NULL;
  }

  char *ret = prekey_encode(serialized, serialized_len);
  free(serialized);

  return ret;
}

static char *receive_dake2(const otrng_prekey_dake2_message_s *msg,
                           otrng_prekey_client_s *client) {
  if (msg->client_instance_tag != client->instance_tag) {
    return NULL;
  }

  if (!otrng_prekey_dake2_message_valid(msg, client)) {
    return NULL;
  }

  return send_dake3(msg, client);
}

static otrng_bool otrng_prekey_storage_status_message_valid(
    const otrng_prekey_storage_status_message_s *msg,
    const uint8_t mac_key[64]) {

  size_t bufl = 1 + 4 + 4;
  uint8_t *buf = malloc(bufl);
  if (!buf) {
    return otrng_false;
  }

  *buf = OTRNG_PREKEY_STORAGE_STATUS_MSG; // message type
  otrng_serialize_uint32(buf + 1, msg->client_instance_tag);
  otrng_serialize_uint32(buf + 5, msg->stored_prekeys);

  // KDF(usage_status_MAC, prekey_mac_k || message type || receiver instance
  // tag
  // || Stored Prekey Messages Number, 64)
  uint8_t mac_tag[64];
  goldilocks_shake256_ctx_p hmac;
  kdf_init_with_usage(hmac, 0x0B);
  hash_update(hmac, mac_key, 64);
  hash_update(hmac, buf, bufl);
  hash_final(hmac, mac_tag, 64);
  hash_destroy(hmac);
  free(buf);

  if (otrl_mem_differ(mac_tag, msg->mac, sizeof(mac_tag)) != 0) {
    sodium_memzero(mac_tag, sizeof(mac_tag));
    return otrng_false;
  }

  return otrng_true;
}

/*
 * Fora:
 * send_storage_status_request
 * send_prekey_storage_message
 * send_prekey_request_message
 *
 * LOOP(
 *   to_send = receive_prekey_server_msg()
 *   send_prekey_server_msg(to_send) if to_send
 * )
 */

static char *
receive_storage_status(const otrng_prekey_storage_status_message_s *msg,
                       otrng_prekey_client_s *client) {
  if (msg->client_instance_tag != client->instance_tag) {
    return NULL;
  }

  if (!otrng_prekey_storage_status_message_valid(msg, client->mac_key)) {
    // TODO: Prekey storage status received is invalid, should it warn
    // the plugin via a callback?
    printf("Received an INVALID storage status message\n");
    return NULL;
  }

  // TODO: Probably we want to invoke a callback to notify the plugin.
  printf("Received Prekey Storage Status message: %d\n", msg->stored_prekeys);

  return NULL;
}

static otrng_err parse_header(uint8_t *message_type, const uint8_t *buf,
                              size_t buflen, size_t *read) {
  size_t r = 0; // read
  size_t w = 0; // walked

  uint16_t protocol_version = 0;
  if (!otrng_deserialize_uint16(&protocol_version, buf, buflen, &r)) {
    return OTRNG_ERROR;
  }

  w += r;

  if (protocol_version != OTRNG_PROTOCOL_VERSION_4) {
    return OTRNG_ERROR;
  }

  if (!otrng_deserialize_uint8(message_type, buf + w, buflen - w, &r)) {
    return OTRNG_ERROR;
  }

  w += r;

  if (read) {
    *read = w;
  }

  return OTRNG_SUCCESS;
}

static char *receive_decoded(const uint8_t *decoded, size_t decoded_len,
                             otrng_prekey_client_s *client) {
  uint8_t message_type = 0;
  if (!parse_header(&message_type, decoded, decoded_len, NULL)) {
    return NULL;
  }

  char *ret = NULL;

  // DAKE 2
  if (message_type == OTRNG_PREKEY_DAKE2_MSG) {
    otrng_prekey_dake2_message_s msg[1];

    if (!otrng_prekey_dake2_message_deserialize(msg, decoded, decoded_len)) {
      return NULL;
    }

    ret = receive_dake2(msg, client);
    otrng_prekey_dake2_message_destroy(msg);
  } else if (message_type == OTRNG_PREKEY_STORAGE_STATUS_MSG) {
    otrng_prekey_storage_status_message_s msg[1];

    if (!otrng_prekey_storage_status_message_deserialize(msg, decoded,
                                                         decoded_len)) {
      return NULL;
    }

    ret = receive_storage_status(msg, client);
    otrng_prekey_storage_status_message_destroy(msg);
  } else if (message_type == 0x06) {
    if (decoded_len < 71) {
      // TODO: The success message is wrong, should we tell the plugin about
      // it via a callback?
      printf("Received an INVALID success message\n");
      return NULL;
    }

    uint8_t mac_tag[64] = {0};
    goldilocks_shake256_ctx_p hash;
    kdf_init_with_usage(hash, 0x0C);
    hash_update(hash, client->mac_key, 64);
    hash_update(hash, decoded + 2, 5);
    hash_final(hash, mac_tag, 64);
    hash_destroy(hash);

    if (otrl_mem_differ(mac_tag, decoded + 7, 64) != 0) {
      // TODO: The success message is wrong, should we tell the plugin about
      // it via a callback?
      printf("Received an INVALID success message\n");
    } else {
      // TODO: The success message is correct, should we tell the plugin about
      // it via a callback?
      printf("Received an VALID success message\n");
    }

    sodium_memzero(mac_tag, sizeof(mac_tag));
  }

  return ret;
}

API otrng_err otrng_prekey_client_receive(char **tosend, const char *server,
                                          const char *message,
                                          otrng_prekey_client_s *client) {

  // I should only process prekey server messages from who I am expecting.
  // This avoids treating a plaintext message "casa." from alice@itr.im as a
  // malformed prekey server message.
  if (strcmp(client->server_identity, server)) {
    return OTRNG_ERROR;
  }

  // TODO: process fragmented messages

  // If it fails to decode it was not a prekey server message.
  uint8_t *serialized = NULL;
  size_t serialized_len = 0;
  if (!prekey_decode(message, &serialized, &serialized_len)) {
    return OTRNG_ERROR;
  }

  // Everything else, returns SUCCESS because we processed the message.
  // Even if there was na error processing it. We should consider informing
  // error while processing using callbacks.
  *tosend = receive_decoded(serialized, serialized_len, client);
  free(serialized);

  return OTRNG_SUCCESS;
}

INTERNAL
otrng_err
otrng_prekey_dake1_message_asprint(uint8_t **serialized, size_t *serialized_len,
                                   const otrng_prekey_dake1_message_s *msg) {

  uint8_t *client_profile_buff = NULL;
  size_t client_profile_buff_len = 0;
  if (!otrng_client_profile_asprintf(&client_profile_buff,
                                     &client_profile_buff_len,
                                     msg->client_profile)) {
    return OTRNG_ERROR;
  }

  size_t ret_len = 2 + 1 + 4 + client_profile_buff_len + ED448_POINT_BYTES;
  uint8_t *ret = malloc(ret_len);
  if (!ret) {
    free(client_profile_buff);
    return OTRNG_ERROR;
  }

  size_t w = 0;
  w += otrng_serialize_uint16(ret + w, OTRNG_PROTOCOL_VERSION_4);
  w += otrng_serialize_uint8(ret + w, OTRNG_PREKEY_DAKE1_MSG);
  w += otrng_serialize_uint32(ret + w, msg->client_instance_tag);
  w += otrng_serialize_bytes_array(ret + w, client_profile_buff,
                                   client_profile_buff_len);
  w += otrng_serialize_ec_point(ret + w, msg->I);
  free(client_profile_buff);

  *serialized = ret;
  if (serialized_len) {
    *serialized_len = w;
  }

  return OTRNG_SUCCESS;
}

INTERNAL
void otrng_prekey_dake1_message_destroy(otrng_prekey_dake1_message_s *msg) {
  if (!msg) {
    return;
  }

  otrng_client_profile_destroy(msg->client_profile);
  otrng_ec_point_destroy(msg->I);
}

INTERNAL otrng_err otrng_prekey_dake2_message_deserialize(
    otrng_prekey_dake2_message_s *dst, const uint8_t *serialized,
    size_t serialized_len) {

  size_t w = 0;
  size_t read = 0;

  uint8_t message_type = 0;
  if (!parse_header(&message_type, serialized, serialized_len, &w)) {
    return OTRNG_ERROR;
  }

  if (message_type != OTRNG_PREKEY_DAKE2_MSG) {
    return OTRNG_ERROR;
  }

  if (!otrng_deserialize_uint32(&dst->client_instance_tag, serialized + w,
                                serialized_len - w, &read)) {
    return OTRNG_ERROR;
  }

  w += read;

  const uint8_t *composite_identity_start = serialized + w;
  if (!otrng_deserialize_data(&dst->server_identity, &dst->server_identity_len,
                              serialized + w, serialized_len - w, &read)) {
    return OTRNG_ERROR;
  }

  w += read;

  if (!otrng_deserialize_otrng_public_key(dst->server_pub_key, serialized + w,
                                          serialized_len - w, &read)) {
    return OTRNG_ERROR;
  }

  w += read;

  // Store the composite identity, so we can use it to generate `t`
  dst->composite_identity_len = serialized + w - composite_identity_start;
  dst->composite_identity = malloc(dst->composite_identity_len);
  if (!dst->composite_identity) {
    return OTRNG_ERROR;
  }
  memcpy(dst->composite_identity, composite_identity_start,
         dst->composite_identity_len);

  if (!otrng_deserialize_ec_point(dst->S, serialized + w, serialized_len - w)) {
    return OTRNG_ERROR;
  }

  w += ED448_POINT_BYTES;

  if (!otrng_deserialize_ring_sig(dst->sigma, serialized + w,
                                  serialized_len - w, NULL)) {
    return OTRNG_ERROR;
  }

  return OTRNG_SUCCESS;
}

INTERNAL
void otrng_prekey_dake2_message_destroy(otrng_prekey_dake2_message_s *msg) {
  if (!msg) {
    return;
  }

  free(msg->composite_identity);
  msg->composite_identity = NULL;

  free(msg->server_identity);
  msg->server_identity = NULL;

  otrng_ec_point_destroy(msg->S);
  otrng_ring_sig_destroy(msg->sigma);
}

INTERNAL otrng_err
otrng_prekey_dake3_message_asprint(uint8_t **serialized, size_t *serialized_len,
                                   const otrng_prekey_dake3_message_s *msg) {
  size_t ret_len =
      2 + 1 + 4 + RING_SIG_BYTES + (4 + msg->message_len) + ED448_POINT_BYTES;
  uint8_t *ret = malloc(ret_len);
  if (!ret) {
    return OTRNG_ERROR;
  }

  size_t w = 0;
  w += otrng_serialize_uint16(ret + w, OTRNG_PROTOCOL_VERSION_4);
  w += otrng_serialize_uint8(ret + w, OTRNG_PREKEY_DAKE3_MSG);
  w += otrng_serialize_uint32(ret + w, msg->client_instance_tag);
  w += otrng_serialize_ring_sig(ret + w, msg->sigma);
  w += otrng_serialize_data(ret + w, msg->message, msg->message_len);

  *serialized = ret;
  if (serialized_len) {
    *serialized_len = w;
  }

  return OTRNG_SUCCESS;
}

INTERNAL
void otrng_prekey_dake3_message_destroy(otrng_prekey_dake3_message_s *msg) {
  if (!msg) {
    return;
  }

  free(msg->message);
  msg->message = NULL;

  otrng_ring_sig_destroy(msg->sigma);
}

INTERNAL otrng_err otrng_prekey_storage_status_message_deserialize(
    otrng_prekey_storage_status_message_s *dst, const uint8_t *serialized,
    size_t serialized_len) {
  size_t w = 0;
  size_t read = 0;

  uint8_t message_type = 0;
  if (!parse_header(&message_type, serialized, serialized_len, &w)) {
    return OTRNG_ERROR;
  }

  if (message_type != OTRNG_PREKEY_STORAGE_STATUS_MSG) {
    return OTRNG_ERROR;
  }

  if (!otrng_deserialize_uint32(&dst->client_instance_tag, serialized + w,
                                serialized_len - w, &read)) {
    return OTRNG_ERROR;
  }

  w += read;

  if (!otrng_deserialize_uint32(&dst->stored_prekeys, serialized + w,
                                serialized_len - w, &read)) {
    return OTRNG_ERROR;
  }

  w += read;

  if (!otrng_deserialize_bytes_array(dst->mac, sizeof(dst->mac), serialized + w,
                                     serialized_len - w)) {
    return OTRNG_ERROR;
  }

  w += sizeof(dst->mac);

  return OTRNG_SUCCESS;
}

INTERNAL
void otrng_prekey_storage_status_message_destroy(
    otrng_prekey_storage_status_message_s *msg) {
  // TODO
}

INTERNAL
void otrng_prekey_publication_message_destroy(
    otrng_prekey_publication_message_s *msg) {
  if (!msg) {
    return;
  }

  if (msg->prekey_messages) {
    for (int i = 0; i < msg->num_prekey_messages; i++) {
      free(msg->prekey_messages[i]);
    }

    free(msg->prekey_messages);
    msg->prekey_messages = NULL;
  }

  otrng_client_profile_free(msg->client_profile);
  msg->client_profile = NULL;

  otrng_prekey_profile_free(msg->prekey_profile);
  msg->prekey_profile = NULL;
}
