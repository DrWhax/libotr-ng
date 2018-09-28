/*
 *  This file is part of the Off-the-Record Next Generation Messaging
 *  library (libotr-ng).
 *
 *  Copyright (C) 2016-2018, the libotr-ng contributors.
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
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

#ifndef S_SPLINT_S
#include <libotr/privkey.h>
#endif

#include <time.h>

#define OTRNG_CLIENT_PRIVATE

#include "alloc.h"
#include "client.h"
#include "client_callbacks.h"
#include "deserialize.h"
#include "instance_tag.h"
#include "messaging.h"
#include "serialize.h"
#include "smp.h"
#include "str.h"

#define MAX_NUMBER_PUBLISHED_PREKEY_MESSAGES 255
#define HEARTBEAT_INTERVAL 60

tstatic otrng_conversation_s *new_conversation_with(const char *recipient,
                                                    otrng_s *conn) {
  otrng_conversation_s *conv = otrng_xmalloc(sizeof(otrng_conversation_s));

  conv->recipient = otrng_xstrdup(recipient);

  conv->conn = conn;

  return conv;
}

tstatic void conversation_free(void *data) {
  otrng_conversation_s *conv = data;

  free(conv->recipient);
  otrng_free(conv->conn);

  free(conv);
}

tstatic otrng_bool should_heartbeat(int last_sent) {
  time_t now = time(NULL);
  int interval = now - HEARTBEAT_INTERVAL;
  if (last_sent < interval) {
    return otrng_true;
  }
  return otrng_false;
}

API otrng_client_s *otrng_client_new(const otrng_client_id_s client_id) {
  otrng_client_s *client = otrng_xmalloc(sizeof(otrng_client_s));
  memset(client, 0, sizeof(otrng_client_s));

  client->client_id = client_id;
  client->max_stored_msg_keys = 1000;
  client->max_published_prekey_msg = 100;
  client->minimum_stored_prekey_msg = 20;
  client->should_heartbeat = should_heartbeat;

#define EXTRA_CLIENT_PROFILE_EXPIRATION_SECONDS 2 * 24 * 60 * 60; /* 2 days */
  client->profiles_extra_valid_time = EXTRA_CLIENT_PROFILE_EXPIRATION_SECONDS;

#define CLIENT_PROFILE_EXPIRATION_SECONDS 2 * 7 * 24 * 60 * 60; /* 2 weeks */
  client->client_profile_exp_time = CLIENT_PROFILE_EXPIRATION_SECONDS;

  return client;
}

tstatic void otrng_stored_prekeys_free(otrng_stored_prekeys_s *s) {
  if (!s) {
    return;
  }

  otrng_ecdh_keypair_destroy(s->our_ecdh);
  free(s->our_ecdh);
  otrng_dh_keypair_destroy(s->our_dh);
  free(s->our_dh);

  free(s);
}

tstatic void stored_prekeys_free_from_list(void *p) {
  otrng_stored_prekeys_free((otrng_stored_prekeys_s *)p);
}

API void otrng_client_free(otrng_client_s *client) {
  if (!client) {
    return;
  }

  otrng_keypair_free(client->keypair);
  if (client->forging_key) {
    otrng_ec_point_destroy(*client->forging_key);
  }
  free(client->forging_key);
  otrng_list_free(client->our_prekeys, stored_prekeys_free_from_list);
  otrng_client_profile_free(client->client_profile);
  otrng_client_profile_free(client->exp_client_profile);
  otrng_prekey_profile_free(client->prekey_profile);
  otrng_prekey_profile_free(client->exp_prekey_profile);
  otrng_shared_prekey_pair_free(client->shared_prekey_pair);
  otrng_list_free(client->conversations, conversation_free);
  otrng_prekey_client_free(client->prekey_client);

  free(client);
}

// TODO: @instance_tag There may be multiple conversations with the same
// recipient if they use multiple instance tags. We are not allowing this yet.
tstatic otrng_conversation_s *
get_conversation_with(const char *recipient, list_element_s *conversations) {
  const list_element_s *el = NULL;
  otrng_conversation_s *conv = NULL;

  for (el = conversations; el; el = el->next) {
    conv = el->data;
    if (!strcmp(conv->recipient, recipient)) {
      return conv;
    }
  }

  return NULL;
}

tstatic otrng_policy_s get_policy_for(const char *recipient) {
  // TODO: @policy the policy should come from client config.
  // or a callback.
  otrng_policy_s policy = {.allows = OTRNG_ALLOW_V3 | OTRNG_ALLOW_V4};
  UNUSED_ARG(recipient);

  return policy;
}

API otrng_bool otrng_conversation_is_encrypted(otrng_conversation_s *conv) {
  if (!conv) {
    return otrng_false;
  }

  switch (conv->conn->running_version) {
  case 0:
    return otrng_false;
  case 3:
    return conv->conn->v3_conn->ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED;
  case 4:
    return conv->conn->state == OTRNG_STATE_ENCRYPTED_MESSAGES;
  }

  return otrng_false;
}

API otrng_bool otrng_conversation_is_finished(otrng_conversation_s *conv) {
  if (!conv) {
    return otrng_false;
  }

  switch (conv->conn->running_version) {
  case 0:
    return otrng_false;
  case 4:
    return conv->conn->state == OTRNG_STATE_FINISHED;
  case 3:
    return conv->conn->v3_conn->ctx->msgstate == OTRL_MSGSTATE_FINISHED;
  }

  return otrng_false;
}

tstatic otrng_s *create_connection_for(const char *recipient,
                                       otrng_client_s *client) {
  otrng_v3_conn_s *v3_conn = NULL;
  otrng_s *conn = NULL;

  v3_conn = otrng_v3_conn_new(client, recipient);
  if (!v3_conn) {
    return NULL;
  }

  conn = otrng_new(client, get_policy_for(recipient));
  if (!conn) {
    otrng_v3_conn_free(v3_conn);
    return NULL;
  }

  conn->peer = otrng_xstrdup(recipient);

  v3_conn->opdata = conn; /* For use in callbacks */
  conn->v3_conn = v3_conn;

  return conn;
}

tstatic otrng_conversation_s *
get_or_create_conversation_with(const char *recipient, otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;
  otrng_s *conn = NULL;

  conv = get_conversation_with(recipient, client->conversations);
  if (conv) {
    return conv;
  }

  conn = create_connection_for(recipient, client);
  if (!conn) {
    return NULL;
  }

  conv = new_conversation_with(recipient, conn);
  if (!conv) {
    free(conn);
    return NULL;
  }

  client->conversations = otrng_list_add(conv, client->conversations);

  return conv;
}

API otrng_conversation_s *
otrng_client_get_conversation(int force_create, const char *recipient,
                              otrng_client_s *client) {
  if (force_create) {
    return get_or_create_conversation_with(recipient, client);
  }

  return get_conversation_with(recipient, client->conversations);
}

// TODO: @client this should allow TLVs to be added to the message
tstatic otrng_result send_message(char **newmsg, const char *message,
                                  const char *recipient,
                                  otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;
  otrng_warning warn = OTRNG_WARN_NONE;
  otrng_result result;

  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return OTRNG_ERROR;
  }

  result = otrng_send_message(newmsg, message, &warn, NULL, 0, conv->conn);

  if (warn == OTRNG_WARN_SEND_NOT_ENCRYPTED) {
    // TODO: we need to signal this a different way than by return values
    /* return OTRNG_CLIENT_RESULT_ERROR_NOT_ENCRYPTED; */
    return OTRNG_ERROR;
  }

  return result;
}

API char *otrng_client_query_message(const char *recipient, const char *message,
                                     otrng_client_s *client) {
  char *ret = NULL;
  otrng_conversation_s *conv = NULL;
  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return NULL;
  }

  if (otrng_failed(otrng_build_query_message(&ret, message, conv->conn))) {
    // TODO: @client This should come from the client (a callback maybe?)
    // because it knows in which language this should be sent, for example.
    char *error = otrng_xstrdup(
        "Failed to start an Off-the-Record private conversation.");
    return error;
  }

  return ret;
}

API otrng_result otrng_client_send(char **newmessage, const char *message,
                                   const char *recipient,
                                   otrng_client_s *client) {
  /* v4 client will know how to transition to v3 if a v3 conversation is
   started */
  return send_message(newmessage, message, recipient, client);
}

API otrng_result otrng_client_send_non_interactive_auth(
    char **newmessage, const prekey_ensemble_s *ensemble, const char *recipient,
    otrng_client_s *client) {
  otrng_conversation_s *conv =
      get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return OTRNG_ERROR;
  }

  return otrng_send_non_interactive_auth(newmessage, ensemble, conv->conn);
}

API otrng_result otrng_client_send_fragment(
    otrng_message_to_send_s **newmessage, const char *message, int mms,
    const char *recipient, otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;
  string_p to_send = NULL;
  uint32_t our_tag, their_tag;
  otrng_result ret;

  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return OTRNG_ERROR;
  }

  if (otrng_failed(send_message(&to_send, message, recipient, client))) {
    free(to_send);
    return OTRNG_ERROR;
  }

  our_tag = otrng_client_get_instance_tag(client);
  their_tag = conv->conn->their_instance_tag;

  ret = otrng_fragment_message(mms, *newmessage, our_tag, their_tag, to_send);
  free(to_send);

  return ret;
}

API otrng_result otrng_client_smp_start(char **tosend, const char *recipient,
                                        const unsigned char *question,
                                        const size_t q_len,
                                        const unsigned char *secret,
                                        size_t secretlen,
                                        otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;

  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return OTRNG_ERROR;
  }

  return otrng_smp_start(tosend, question, q_len, secret, secretlen,
                         conv->conn);
}

API otrng_result otrng_client_smp_respond(char **tosend, const char *recipient,
                                          const unsigned char *secret,
                                          size_t secretlen,
                                          otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;

  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    return OTRNG_ERROR;
  }

  return otrng_smp_continue(tosend, secret, secretlen, conv->conn);
}

API otrng_result otrng_client_receive(char **newmessage, char **todisplay,
                                      const char *message,
                                      const char *recipient,
                                      otrng_client_s *client,
                                      otrng_bool *should_ignore) {
  otrng_result result = OTRNG_ERROR;
  otrng_response_s *response = NULL;
  otrng_conversation_s *conv = NULL;
  otrng_warning warn;

  *should_ignore = otrng_false;

  if (!client) {
    return OTRNG_ERROR;
  }

  if (!newmessage) {
    return result;
  }

  *newmessage = NULL;

  conv = get_or_create_conversation_with(recipient, client);
  if (!conv) {
    *should_ignore = otrng_true;
    return OTRNG_SUCCESS;
  }

  response = otrng_response_new();
  warn = OTRNG_WARN_NONE;

  result = otrng_receive_message(response, &warn, message, conv->conn);

  if (warn == OTRNG_WARN_RECEIVED_NOT_VALID) {
    //    return OTRNG_CLIENT_RESULT_ERROR_NOT_VALID;
    // TODO: fix this
    otrng_response_free(response);
    return OTRNG_ERROR;
  }

  if (response->to_send) {
    *newmessage = otrng_xstrdup(response->to_send);
  }

  *todisplay = NULL;
  if (response->to_display) {
    char *plain = otrng_xstrdup(response->to_display);
    *todisplay = plain;
    otrng_response_free(response);
    return OTRNG_SUCCESS;
  }

  otrng_response_free(response);

  return result;
}

tstatic void destroy_client_conversation(const otrng_conversation_s *conv,
                                         otrng_client_s *client) {
  list_element_s *elem = otrng_list_get_by_value(conv, client->conversations);
  client->conversations =
      otrng_list_remove_element(elem, client->conversations);
  otrng_list_free_nodes(elem);
}

API otrng_result otrng_client_disconnect(char **newmsg, const char *recipient,
                                         otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;

  conv = get_conversation_with(recipient, client->conversations);
  if (!conv) {
    return OTRNG_ERROR;
  }

  if (otrng_failed(otrng_close(newmsg, conv->conn))) {
    return OTRNG_ERROR;
  }

  destroy_client_conversation(conv, client);
  conversation_free(conv);

  return OTRNG_SUCCESS;
}

// TODO: @client this depends on how is going to be handled: as a different
// event or inside process_conv_updated?
/* expiration time should be set on seconds */
API otrng_result otrng_expire_encrypted_session(char **newmsg,
                                                const char *recipient,
                                                int expiration_time,
                                                otrng_client_s *client) {
  otrng_conversation_s *conv = NULL;
  time_t now;

  conv = get_conversation_with(recipient, client->conversations);
  if (!conv) {
    return OTRNG_ERROR;
  }

  now = time(NULL);
  if (conv->conn->keys->last_generated < now - expiration_time) {
    if (otrng_failed(otrng_expire_session(newmsg, conv->conn))) {
      return OTRNG_ERROR;
    }
  }

  destroy_client_conversation(conv, client);
  conversation_free(conv);

  return OTRNG_SUCCESS;
}

API otrng_result otrng_client_expire_fragments(int expiration_time,
                                               otrng_client_s *client) {
  const list_element_s *el = NULL;
  otrng_conversation_s *conv = NULL;
  time_t now;

  now = time(NULL);
  for (el = client->conversations; el; el = el->next) {
    conv = el->data;
    if (otrng_failed(otrng_expire_fragments(now, expiration_time,
                                            &conv->conn->pending_fragments))) {
      return OTRNG_ERROR;
    }
  }

  return OTRNG_SUCCESS;
}

API otrng_result otrng_client_get_our_fingerprint(
    otrng_fingerprint fp, const otrng_client_s *client) {
  if (!client->keypair) {
    return OTRNG_ERROR;
  }

  return otrng_serialize_fingerprint(fp, client->keypair->pub);
}

API otrng_prekey_client_s *
otrng_client_get_prekey_client(const char *server_identity,
                               otrng_prekey_client_callbacks_s *callbacks,
                               otrng_client_s *client) {
  char *account = NULL;
  char *protocol = NULL;

  if (client->prekey_client) {
    return client->prekey_client;
  }
  if (otrng_failed(
          otrng_client_get_account_and_protocol(&account, &protocol, client))) {
    return NULL;
  }
  free(protocol);

  // TODO: this should be a hashmap, since it its one client PER server
  client->prekey_client = otrng_prekey_client_new();
  otrng_prekey_client_init(client->prekey_client, server_identity, account,
                           otrng_client_get_instance_tag(client),
                           otrng_client_get_keypair_v4(client),
                           otrng_client_get_client_profile(client),
                           otrng_client_get_prekey_profile(client),
                           otrng_client_get_max_published_prekey_msg(client),
                           otrng_client_get_minimum_stored_prekey_msg(client));

  free(account);

  client->prekey_client->callbacks = callbacks;

  return client->prekey_client;
}

INTERNAL void otrng_client_store_my_prekey_message(
    uint32_t id, uint32_t instance_tag, const ecdh_keypair_s *ecdh_pair,
    const dh_keypair_s *dh_pair, otrng_client_s *client) {
  otrng_stored_prekeys_s *stored_prekey_msg;
  if (!client) {
    return;
  }

  stored_prekey_msg = otrng_xmalloc(sizeof(otrng_stored_prekeys_s));
  stored_prekey_msg->our_ecdh = otrng_secure_alloc(sizeof(ecdh_keypair_s));
  stored_prekey_msg->our_dh = otrng_secure_alloc(sizeof(dh_keypair_s));
  stored_prekey_msg->id = id;
  stored_prekey_msg->sender_instance_tag = instance_tag;

  /* @secret the keypairs should be deleted once the double ratchet gets
   * initialized */
  otrng_ec_scalar_copy(stored_prekey_msg->our_ecdh->priv, ecdh_pair->priv);
  otrng_ec_point_copy(stored_prekey_msg->our_ecdh->pub, ecdh_pair->pub);
  stored_prekey_msg->our_dh->priv = otrng_dh_mpi_copy(dh_pair->priv);
  stored_prekey_msg->our_dh->pub = otrng_dh_mpi_copy(dh_pair->pub);

  client->our_prekeys = otrng_list_add(stored_prekey_msg, client->our_prekeys);
}

API dake_prekey_message_s **
otrng_client_build_prekey_messages(uint8_t num_messages, otrng_client_s *client,
                                   ec_scalar **ecdh_keys, dh_mpi **dh_keys) {
  uint32_t instance_tag;
  dake_prekey_message_s **messages;
  int i, j;
  ec_scalar *ke;
  dh_mpi *kd;

  if (num_messages > MAX_NUMBER_PUBLISHED_PREKEY_MESSAGES) {
    // TODO: notify error
    return NULL;
  }

  instance_tag = otrng_client_get_instance_tag(client);

  messages = otrng_xmalloc(num_messages * sizeof(dake_prekey_message_s *));
  ke = otrng_secure_alloc(num_messages * sizeof(ec_scalar));
  kd = otrng_xmalloc(num_messages * sizeof(dh_mpi));

  for (i = 0; i < num_messages; i++) {
    ecdh_keypair_s ecdh;
    dh_keypair_s dh;
    otrng_generate_ephemeral_keys(&ecdh, &dh);

    messages[i] =
        otrng_dake_prekey_message_build(instance_tag, ecdh.pub, dh.pub);
    if (!messages[i]) {
      for (j = 0; j < i; j++) {
        otrng_dake_prekey_message_free(messages[j]);
      }
      free(messages);
      return NULL;
    }
    goldilocks_448_scalar_copy(ke[i], ecdh.priv);
    kd[i] = otrng_dh_mpi_copy(dh.priv);

    otrng_client_store_my_prekey_message(
        messages[i]->id, messages[i]->sender_instance_tag, &ecdh, &dh, client);

    // TODO: ecdh_keypair_destroy()
    // dh_keypair_detroy()
  }

  *ecdh_keys = ke;
  *dh_keys = kd;

  return messages;
}

#ifdef DEBUG_API

#include "debug.h"

API void otrng_client_debug_print(FILE *f, int indent, otrng_client_s *c) {
  int ix;
  list_element_s *curr;

  if (otrng_debug_print_should_ignore("client")) {
    return;
  }

  otrng_print_indent(f, indent);
  debug_api_print(f, "client(");
  otrng_debug_print_pointer(f, c);
  debug_api_print(f, ") {\n");

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("client->conversations")) {
    debug_api_print(f, "conversations = IGNORED\n");
  } else {
    debug_api_print(f, "conversations = {\n");
    ix = 0;
    curr = c->conversations;
    while (curr) {
      otrng_print_indent(f, indent + 4);
      debug_api_print(f, "[%d] = {\n", ix);
      otrng_conversation_debug_print(f, indent + 6, curr->data);
      otrng_print_indent(f, indent + 4);
      debug_api_print(f, "} // [%d]\n", ix);
      curr = curr->next;
      ix++;
    }
    otrng_print_indent(f, indent + 2);
    debug_api_print(f, "} // conversations\n");
  }

  // TODO / DEBUG_API: implement
  /* otrng_prekey_client_s *prekey_client; */

  otrng_print_indent(f, indent);
  debug_api_print(f, "} // client\n");
}

API void otrng_conversation_debug_print(FILE *f, int indent,
                                        otrng_conversation_s *c) {
  if (otrng_debug_print_should_ignore("conversation")) {
    return;
  }

  otrng_print_indent(f, indent);
  debug_api_print(f, "conversation(");
  otrng_debug_print_pointer(f, c);
  debug_api_print(f, ") {\n");

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("conversation->conversation_id")) {
    debug_api_print(f, "conversation_id = IGNORED\n");
  } else {
    debug_api_print(f, "conversation_id = ");
    otrng_debug_print_pointer(f, c->conversation_id);
    debug_api_print(f, "\n");
  }

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("conversation->recipient")) {
    debug_api_print(f, "recipient = IGNORED\n");
  } else {
    debug_api_print(f, "recipient = %s\n", c->recipient);
  }

  // TODO / DEBUG_API: implement
  /* otrng_s *conn */

  otrng_print_indent(f, indent);
  debug_api_print(f, "} // conversation\n");
}

#endif /* DEBUG */

tstatic otrng_result get_account_and_protocol_cb(char **account,
                                                 char **protocol,
                                                 const otrng_client_s *client) {
  if (!client->global_state->callbacks ||
      !client->global_state->callbacks->get_account_and_protocol) {
    return OTRNG_ERROR;
  }

  return client->global_state->callbacks->get_account_and_protocol(
      account, protocol, client->client_id);
}

INTERNAL otrng_result otrng_client_get_account_and_protocol(
    char **account, char **protocol, const otrng_client_s *client) {
  return get_account_and_protocol_cb(account, protocol, client);
}

INTERNAL OtrlPrivKey *
otrng_client_get_private_key_v3(const otrng_client_s *client) {
  OtrlPrivKey *ret = NULL;

  // TODO: We could use a "get storage key" callback and use it as
  // account_name plus an arbitrary "libotrng-storage" protocol.
  char *account_name = NULL;
  char *protocol_name = NULL;
  if (!get_account_and_protocol_cb(&account_name, &protocol_name, client)) {
    return ret;
  }

  ret = otrl_privkey_find(client->global_state->user_state_v3, account_name,
                          protocol_name);

  free(account_name);
  free(protocol_name);
  return ret;
}

INTERNAL otrng_keypair_s *otrng_client_get_keypair_v4(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->keypair) {
    return client->keypair;
  }

  /* @secret_information: the long-term key pair lives for as long the client
     decides */
  // TODO @orchestration remove this when orchestration is done
  fprintf(stderr,
          "client.c otrng_client_get_keypair_v4 -> creating private key\n");
  otrng_client_callbacks_create_privkey_v4(client->global_state->callbacks,
                                           client->client_id);

  return client->keypair;
}

INTERNAL otrng_result otrng_client_add_private_key_v4(
    otrng_client_s *client, const uint8_t sym[ED448_PRIVATE_BYTES]) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->keypair) {
    return OTRNG_ERROR;
  }

  /* @secret_information: the long-term key pair lives for as long the client
     decides */
  client->keypair = otrng_keypair_new();
  if (!client->keypair) {
    return OTRNG_ERROR;
  }

  otrng_keypair_generate(client->keypair, sym);
  return OTRNG_SUCCESS;
}

INTERNAL otrng_public_key *
otrng_client_get_forging_key(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (!client->forging_key) {
    otrng_client_callbacks_create_forging_key(client->global_state->callbacks,
                                              client->client_id);
  }

  return client->forging_key;
}

INTERNAL void otrng_client_ensure_forging_key(otrng_client_s *client) {
  if (!client || client->forging_key) {
    return;
  }

  otrng_client_callbacks_create_forging_key(client->global_state->callbacks,
                                            client->client_id);
}

INTERNAL otrng_result otrng_client_add_forging_key(otrng_client_s *client,
                                                   const otrng_public_key key) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->forging_key) {
    return OTRNG_ERROR;
  }

  client->forging_key = otrng_xmalloc(sizeof(otrng_public_key));

  otrng_ec_point_copy(*client->forging_key, key);

  return OTRNG_SUCCESS;
}

API const client_profile_s *
otrng_client_get_client_profile(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->client_profile) {
    return client->client_profile;
  }

  otrng_client_callbacks_create_client_profile(client->global_state->callbacks,
                                               client, client->client_id);

  return client->client_profile;
}

API client_profile_s *
otrng_client_build_default_client_profile(otrng_client_s *client) {
  // TODO: Get allowed versions from the policy
  const char *allowed_versions = "34";
  if (!client) {
    return NULL;
  }

  otrng_client_ensure_forging_key(client);
  return otrng_client_profile_build(
      otrng_client_get_instance_tag(client), allowed_versions,
      otrng_client_get_keypair_v4(client), *client->forging_key,
      otrng_client_get_client_profile_exp_time(client));
}

API otrng_result otrng_client_add_client_profile(
    otrng_client_s *client, const client_profile_s *profile) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->client_profile) {
    return OTRNG_ERROR;
  }

  client->client_profile = otrng_xmalloc(sizeof(client_profile_s));

  otrng_client_profile_copy(client->client_profile, profile);

  return OTRNG_SUCCESS;
}

API const client_profile_s *
otrng_client_get_exp_client_profile(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->exp_client_profile) {
    return client->exp_client_profile;
  }

  return NULL;
}

API otrng_result otrng_client_add_exp_client_profile(
    otrng_client_s *client, const client_profile_s *exp_profile) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->exp_client_profile) {
    return OTRNG_ERROR;
  }

  client->exp_client_profile = otrng_xmalloc(sizeof(client_profile_s));

  otrng_client_profile_copy(client->exp_client_profile, exp_profile);

  return OTRNG_SUCCESS;
}

INTERNAL otrng_result otrng_client_add_shared_prekey_v4(
    otrng_client_s *client, const uint8_t sym[ED448_PRIVATE_BYTES]) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->shared_prekey_pair) {
    return OTRNG_ERROR;
  }

  /* @secret_information: the shared keypair lives for as long the client
     decides */
  client->shared_prekey_pair = otrng_shared_prekey_pair_new();
  if (!client->shared_prekey_pair) {
    return OTRNG_ERROR;
  }

  otrng_shared_prekey_pair_generate(client->shared_prekey_pair, sym);
  return OTRNG_SUCCESS;
}

static const otrng_shared_prekey_pair_s *
get_shared_prekey_pair(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->shared_prekey_pair) {
    return client->shared_prekey_pair;
  }

  otrng_client_callbacks_create_shared_prekey(client->global_state->callbacks,
                                              client, client->client_id);

  return client->shared_prekey_pair;
}

API const otrng_prekey_profile_s *
otrng_client_get_prekey_profile(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->prekey_profile) {
    return client->prekey_profile;
  }

  otrng_client_callbacks_create_prekey_profile(client->global_state->callbacks,
                                               client, client->client_id);

  return client->prekey_profile;
}

API otrng_prekey_profile_s *
otrng_client_build_default_prekey_profile(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  /* @secret: the shared prekey should be deleted once the prekey profile
   * expires */
  return otrng_prekey_profile_build(otrng_client_get_instance_tag(client),
                                    otrng_client_get_keypair_v4(client),
                                    get_shared_prekey_pair(client));
}

API otrng_result otrng_client_add_prekey_profile(
    otrng_client_s *client, const otrng_prekey_profile_s *profile) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->prekey_profile) {
    return OTRNG_ERROR;
  }

  client->prekey_profile = otrng_xmalloc(sizeof(otrng_prekey_profile_s));

  otrng_prekey_profile_copy(client->prekey_profile, profile);

  return OTRNG_SUCCESS;
}

API const otrng_prekey_profile_s *
otrng_client_get_exp_prekey_profile(otrng_client_s *client) {
  if (!client) {
    return NULL;
  }

  if (client->exp_prekey_profile) {
    return client->prekey_profile;
  }

  return NULL;
}

API otrng_result otrng_client_add_exp_prekey_profile(
    otrng_client_s *client, const otrng_prekey_profile_s *exp_profile) {
  if (!client) {
    return OTRNG_ERROR;
  }

  if (client->exp_prekey_profile) {
    return OTRNG_ERROR;
  }

  client->exp_prekey_profile = otrng_xmalloc(sizeof(otrng_prekey_profile_s));

  otrng_prekey_profile_copy(client->exp_prekey_profile, exp_profile);

  return OTRNG_SUCCESS;
}

tstatic OtrlInsTag *otrng_instance_tag_new(const char *protocol,
                                           const char *account,
                                           unsigned int instag) {
  OtrlInsTag *p;
  if (instag < OTRNG_MIN_VALID_INSTAG) {
    return NULL;
  }

  p = otrng_xmalloc(sizeof(OtrlInsTag));

  p->accountname = otrng_xstrdup(account);
  p->protocol = otrng_xstrdup(protocol);
  p->instag = instag;

  return p;
}

tstatic void otrl_userstate_instance_tag_add(OtrlUserState us, OtrlInsTag *p) {
  // This comes from libotr
  p->next = us->instag_root;
  if (p->next) {
    p->next->tous = &(p->next);
  }

  p->tous = &(us->instag_root);
  us->instag_root = p;
}

INTERNAL unsigned int
otrng_client_get_instance_tag(const otrng_client_s *client) {
  char *account_name = NULL;
  char *protocol_name = NULL;
  OtrlInsTag *instag;

  if (!client->global_state->user_state_v3) {
    return (unsigned int)0;
  }

  // TODO: We could use a "get storage key" callback and use it as
  // account_name plus an arbitrary "libotrng-storage" protocol.
  if (!get_account_and_protocol_cb(&account_name, &protocol_name, client)) {
    return (unsigned int)1;
  }

  instag = otrl_instag_find(client->global_state->user_state_v3, account_name,
                            protocol_name);

  free(account_name);
  free(protocol_name);

  if (!instag) {
    otrng_client_callbacks_create_instag(client->global_state->callbacks,
                                         client->client_id);
  }

  if (!instag) {
    return (unsigned int)0;
  }

  return instag->instag;
}

INTERNAL otrng_result otrng_client_add_instance_tag(otrng_client_s *client,
                                                    unsigned int instag) {
  char *account_name = NULL;
  char *protocol_name = NULL;
  OtrlInsTag *p;

  if (!client) {
    return OTRNG_ERROR;
  }

  if (!client->global_state->user_state_v3) {
    return OTRNG_ERROR;
  }

  // TODO: We could use a "get storage key" callback and use it as
  // account_name plus an arbitrary "libotrng-storage" protocol.
  if (!get_account_and_protocol_cb(&account_name, &protocol_name, client)) {
    return OTRNG_ERROR;
  }

  p = otrl_instag_find(client->global_state->user_state_v3, account_name,
                       protocol_name);
  if (p) {
    free(account_name);
    free(protocol_name);
    return OTRNG_ERROR;
  }

  p = otrng_instance_tag_new(protocol_name, account_name, instag);

  free(account_name);
  free(protocol_name);
  if (!p) {
    return OTRNG_ERROR;
  }

  otrl_userstate_instance_tag_add(client->global_state->user_state_v3, p);
  return OTRNG_SUCCESS;
}

tstatic list_element_s *get_stored_prekey_node_by_id(uint32_t id,
                                                     list_element_s *l) {
  while (l) {
    const otrng_stored_prekeys_s *s = l->data;
    if (!s) {
      continue;
    }

    if (s->id == id) {
      return l;
    }

    l = l->next;
  }

  return NULL;
}

INTERNAL const otrng_stored_prekeys_s *
otrng_client_get_my_prekeys_by_id(uint32_t id, const otrng_client_s *client) {
  list_element_s *node = get_stored_prekey_node_by_id(id, client->our_prekeys);
  if (!node) {
    return NULL;
  }

  return node->data;
}

INTERNAL void
otrng_client_delete_my_prekey_message_by_id(uint32_t id,
                                            otrng_client_s *client) {
  list_element_s *node = get_stored_prekey_node_by_id(id, client->our_prekeys);
  if (!node) {
    return;
  }

  client->our_prekeys = otrng_list_remove_element(node, client->our_prekeys);
  otrng_list_free(node, stored_prekeys_free_from_list);
}

API void otrng_client_set_padding(size_t granularity, otrng_client_s *client) {
  client->padding = granularity;
}

API void otrng_client_set_max_stored_msg_keys(unsigned int max_stored_msg_keys,
                                              otrng_client_s *client) {
  client->max_stored_msg_keys = max_stored_msg_keys;
}

API otrng_result
otrng_client_get_max_published_prekey_msg(otrng_client_s *client) {
  if (!client) {
    return OTRNG_ERROR;
  }

  return client->max_published_prekey_msg;
}

API void
otrng_client_set_max_published_prekey_msg(unsigned int max_published_prekey_msg,
                                          otrng_client_s *client) {
  client->max_published_prekey_msg = max_published_prekey_msg;
}

API otrng_result
otrng_client_get_minimum_stored_prekey_msg(otrng_client_s *client) {
  if (!client) {
    return OTRNG_ERROR;
  }

  return client->minimum_stored_prekey_msg;
}

API void otrng_client_state_set_minimum_stored_prekey_msg(
    unsigned int minimum_stored_prekey_msg, otrng_client_s *client) {
  client->minimum_stored_prekey_msg = minimum_stored_prekey_msg;
}

API void
otrng_client_set_profiles_extra_valid_time(uint64_t profiles_extra_valid_time,
                                           otrng_client_s *client) {
  client->profiles_extra_valid_time = profiles_extra_valid_time;
}

API otrng_result
otrng_client_get_client_profile_exp_time(otrng_client_s *client) {
  if (!client) {
    return OTRNG_ERROR;
  }

  return client->client_profile_exp_time;
}

API void
otrng_client_set_client_profile_exp_time(uint64_t client_profile_exp_time,
                                         otrng_client_s *client) {
  client->client_profile_exp_time = client_profile_exp_time;
}

API otrng_result
otrng_client_get_prekey_profile_exp_time(otrng_client_s *client) {
  if (!client) {
    return OTRNG_ERROR;
  }

  return client->prekey_profile_exp_time;
}

API void
otrng_client_set_prekey_profile_exp_time(uint64_t prekey_profile_exp_time,
                                         otrng_client_s *client) {
  client->prekey_profile_exp_time = prekey_profile_exp_time;
}

#ifdef DEBUG_API

API void otrng_stored_prekeys_debug_print(FILE *f, int indent,
                                          otrng_stored_prekeys_s *s) {
  if (otrng_debug_print_should_ignore("stored_prekeys")) {
    return;
  }

  otrng_print_indent(f, indent);
  debug_api_print(f, "stored_prekeys(");
  otrng_debug_print_pointer(f, s);
  debug_api_print(f, ") {\n");

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("stored_prekeys->id")) {
    debug_api_print(f, "id = IGNORED\n");
  } else {
    debug_api_print(f, "id = %x\n", s->id);
  }

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("stored_prekeys->sender_instance_tag")) {
    debug_api_print(f, "sender_instance_tag = IGNORED\n");
  } else {
    debug_api_print(f, "sender_instance_tag = %x\n", s->sender_instance_tag);
  }

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("stored_prekeys->our_ecdh")) {
    debug_api_print(f, "our_ecdh = IGNORED\n");
  } else {
    debug_api_print(f, "our_ecdh = {\n");
    otrng_ecdh_keypair_debug_print(f, indent + 4, s->our_ecdh);
    otrng_print_indent(f, indent + 2);
    debug_api_print(f, "} // our_ecdh\n");
  }

  otrng_print_indent(f, indent + 2);
  if (otrng_debug_print_should_ignore("stored_prekeys->our_dh")) {
    debug_api_print(f, "our_dh = IGNORED\n");
  } else {
    debug_api_print(f, "our_dh = {\n");
    otrng_dh_keypair_debug_print(f, indent + 4, s->our_dh);
    otrng_print_indent(f, indent + 2);
    debug_api_print(f, "} // our_dh\n");
  }

  otrng_print_indent(f, indent);
  debug_api_print(f, "} // stored_prekeys\n");
}

#endif /* DEBUG */
