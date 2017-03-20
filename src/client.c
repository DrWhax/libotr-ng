#include "client.h"

#include "str.h"
#include "serialize.h"
#include "sha3.h"
#include "cramershoup_interface.h"

void conversation_free(otr4_conversation_t * conv)
{
	otrv4_free(conv->conn);
	conv->conn = NULL;

	free(conv->recipient);
	conv->recipient = NULL;
}

otr4_client_t *otr4_client_new(cs_keypair_s * keypair)
{
	otr4_client_t *client = malloc(sizeof(otr4_client_t));
	if (!client)
		return NULL;

	client->keypair = keypair;
	client->conversations = NULL;
	client->callbacks = NULL;

	return client;
}

void otr4_client_free(otr4_client_t * client)
{
	list_foreach(client->conversations, c, {
		     conversation_free((otr4_conversation_t *) c->data);
		     c->data = NULL;
		     });
	list_free_all(client->conversations);
	client->conversations = NULL;
	client->keypair = NULL;

	free(client);
}

otr4_conversation_t *get_conversation_with(const char *recipient,
					   list_element_t * conversations)
{
	list_foreach(conversations, c, {
		     otr4_conversation_t * conv =
		     (otr4_conversation_t *) c->data;
		     if (!strcmp(conv->recipient, recipient)) return conv;}
	) ;

	return NULL;
}

otr4_conversation_t *new_conversation_with(const char *recipient)
{
	otr4_conversation_t *conv = malloc(sizeof(otr4_conversation_t));
	if (!conv)
		return NULL;

	conv->recipient = otrv4_strdup(recipient);

	return conv;
}

otr4_conversation_t *get_or_create_conversation_with(const char *recipient,
						     otr4_client_t * client)
{
	otr4_conversation_t *conv = NULL;

	if (!client->keypair)
		return NULL;

	conv = get_conversation_with(recipient, client->conversations);
	if (!conv) {
		conv = new_conversation_with(recipient);
		//TODO the policy should come from client config.
		otrv4_policy_t policy = {.allows =
			    OTRV4_ALLOW_V3 | OTRV4_ALLOW_V4
		};
		conv->conn = otrv4_new(client->keypair, policy);
		client->conversations = list_add(conv, client->conversations);
	}

	return conv;
}

otr4_conversation_t *otr4_client_get_conversation(int force,
						  const char *recipient,
						  otr4_client_t * client)
{
	if (force)
		return get_or_create_conversation_with(recipient, client);

	return get_conversation_with(recipient, client->conversations);
}

int
otr4_client_send(char **newmessage, const char *message,
		 const char *recipient, otr4_client_t * client)
{
	otr4_conversation_t *conv =
	    get_or_create_conversation_with(recipient, client);

	if (conv->conn->state == OTRV4_STATE_START) {
		return 1;
	}
	//TODO: add notifications (like "ttried to send a message while not in
	//encrypted")
	*newmessage = NULL;
	if (!otrv4_send_message
	    ((unsigned char **)newmessage, (unsigned char *)message,
	     strlen(message) + 1, conv->conn)) {
		return -1;
	}

	return 0;
}

int
otr4_client_receive(char **newmessage, char **todisplay, const char *message,
		    const char *recipient, otr4_client_t * client)
{
	otrv4_state state_before;
	*newmessage = NULL;
	*todisplay = NULL;

	otr4_conversation_t *conv =
	    get_or_create_conversation_with(recipient, client);
	state_before = conv->conn->state;

	otrv4_response_t *response = otrv4_response_new();
	if (!otrv4_receive_message
	    (response, (const string_t)message, strlen(message), conv->conn)) {
		otrv4_response_free(response);
		return 0;	//Should this cause the message to be ignored or not?
	}

	if (state_before != OTRV4_STATE_ENCRYPTED_MESSAGES
	    && conv->conn->state == OTRV4_STATE_ENCRYPTED_MESSAGES) {
		if (client->callbacks && client->callbacks->gone_secure)
			client->callbacks->gone_secure(conv);
	}

	if (response->to_send) {
		char *tosend = otrv4_strdup(response->to_send);
		*newmessage = tosend;
	}

	int should_ignore = 1;
	if (response->to_display) {
		char *plain = otrv4_strdup(response->to_display);
		*todisplay = plain;
		should_ignore = 0;
	}

	otrv4_response_free(response);
	return should_ignore;
}

char *otr4_client_query_message(const char *recipient, const char *message,
				otr4_client_t * client)
{
	otr4_conversation_t *conv =
	    get_or_create_conversation_with(recipient, client);

	//TODO: implement policy
	char *ret = NULL;
	otrv4_build_query_message(&ret, conv->conn, (const string_t)message,
				  strlen(message));
	return ret;
}

void
otr4_client_disconnect(char **newmessage, const char *recipient,
		       otr4_client_t * client)
{
	//TODO
}

uint8_t *otr4_client_get_our_fingerprint(const otr4_client_t * client)
{
	uint8_t serialized[170] = { 0 };
	uint8_t *ser = NULL;

	if (!client->keypair)
		return NULL;

	ser = malloc(64);
	if (!ser)
		return NULL;

	//TODO: do we need to check anything? 
	serialize_cs_public_key(serialized, client->keypair->pub);

	if (sha3_512(ser, 64, serialized, sizeof(serialized)))
		return ser;

	free(ser);
	return NULL;
}

int otr4_privkey_generate_FILEp(const otr4_client_t * client, FILE * privf)
{
	char *buff = NULL;
	size_t s = 0;
	int err = 0;

	if (!privf)
		return -1;

	if (!client->keypair)
		return -2;

	err = cs_serialize_private_key(&buff, &s, client->keypair->priv);
	if (err)
		return err;

	if (1 != fwrite(buff, s, 1, privf))
		return -3;

	return 0;
}

static cs_keypair_s* new_keypair()
{
        cs_keypair_s *pair = NULL;

        pair = malloc(sizeof(cs_keypair_s));
	if (pair)
                cs_keypair_destroy(pair);

        return pair;
}

int otr4_read_privkey_FILEp(otr4_client_t * client, FILE * privf)
{
	if (!privf)
		return -1;

	if (!client->keypair)
		client->keypair = new_keypair();

	if (!client->keypair)
		return -2;

	if (cs_deserialize_private_key_FILEp(client->keypair->priv, privf)) {
                cs_keypair_destroy(client->keypair);
                free(client->keypair);
                client->keypair = NULL;
                return -3;
        }

	cs_keypair_derive_public_key(client->keypair);
	return 0;
}

/* Convert a 64-byte hash value to a 145-byte human-readable value */
void
otr4_fingerprint_hash_to_human(char human[OTR4_FPRINT_HUMAN_LEN],
			       const unsigned char hash[OTR4_FPRINT_LEN_BYTES])
{
	int word, byte;
	char *p = human;

	for (word = 0; word < 16; ++word) {
		for (byte = 0; byte < 4; ++byte) {
			sprintf(p, "%02X", hash[word * 4 + byte]);
			p += 2;
		}
		*(p++) = ' ';
	}

	/* Change that last ' ' to a '\0' */
	--p;
	*p = '\0';
}
