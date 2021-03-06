/*
 *  This file is part of the Off-the-Record Next Generation Messaging
 *  library (libotr-ng).
 *
 *  Copyright (C) 2016-2019, the libotr-ng contributors.
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

/**
 * The functions in this file only operate on their arguments, and doesn't touch
 * any global state. It is safe to call these functions concurrently from
 * different threads, as long as arguments pointing to the same memory areas are
 * not used from different threads.
 */

#ifndef OTRNG_SERIALIZE_H
#define OTRNG_SERIALIZE_H

#include <stdint.h>

#include "auth.h"
#include "dh.h"
#include "ed448.h"
#include "error.h"
#include "list.h"
#include "mpi.h"
#include "shared.h"

#define CRAMER_SHOUP_PUBKEY_BYTES 170

INTERNAL size_t otrng_serialize_uint64(uint8_t *dst, const uint64_t data);

INTERNAL size_t otrng_serialize_uint32(uint8_t *dst, const uint32_t data);

INTERNAL size_t otrng_serialize_uint16(uint8_t *dst, const uint16_t data);

INTERNAL size_t otrng_serialize_uint8(uint8_t *dst, const uint8_t data);

INTERNAL size_t otrng_serialize_bytes_array(uint8_t *target,
                                            const uint8_t data[], size_t len);

INTERNAL size_t otrng_serialize_data(uint8_t *target,
                                     const uint8_t /*@null@*/ *data,
                                     size_t len);

INTERNAL int otrng_serialize_ec_point(uint8_t *dst, const ec_point point);

INTERNAL size_t otrng_serialize_ec_scalar(uint8_t *dst, const ec_scalar scalar);

INTERNAL otrng_result otrng_serialize_dh_mpi_otr(uint8_t *dst, size_t dst_len,
                                                 size_t *written,
                                                 const dh_mpi mpi);

/**
 * @brief Serializes a DH public key as an MPI.
 *
 * @warning MPIs use the minimum-length encoding; i. e. no leading zeroes.
 *
 * @param [dst] The destination.
 * @param [dst_len] The length of destination.
 * @param [written] How many bytes were written to destination.
 * @param [pub] The DH public key.
 *
 * @out OTRNG_SUCCESS or OTRNG_ERROR.
 */
INTERNAL otrng_result otrng_serialize_dh_public_key(uint8_t *dst,
                                                    size_t dst_len,
                                                    size_t *written,
                                                    const dh_public_key pub);

INTERNAL size_t otrng_serialize_ring_sig(uint8_t *dst, const ring_sig_s *proof);

INTERNAL size_t otrng_serialize_public_key(uint8_t *dst,
                                           const otrng_public_key);

INTERNAL size_t otrng_serialize_forging_key(uint8_t *dst,
                                            const otrng_public_key);

INTERNAL size_t otrng_serialize_shared_prekey(
    uint8_t *dst, const otrng_shared_prekey_pub shared_prekey);

/**
 * @brief Serialize the old mac keys to reveal.
 *
 * @param [old_mac_keys]   The list of old mac keys.
 */
/*@null@*/ INTERNAL uint8_t *
otrng_serialize_old_mac_keys(list_element_s *old_mac_keys);

INTERNAL size_t otrng_serialize_phi(uint8_t *dst,
                                    const char *shared_session_state,
                                    uint16_t sender_instance_tag,
                                    uint16_t receiver_instance_tag);

#ifdef OTRNG_SERIALIZE_PRIVATE
#endif

#endif
