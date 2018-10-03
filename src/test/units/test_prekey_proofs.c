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

#include <glib.h>

#include "test_helpers.h"

#include "test_fixtures.h"

#include "prekey_proofs.h"

static void test_ecdh_proof_generation_and_validation(void) {
  otrng_keypair_s v1, v2, v3, v4;
  uint8_t sym1[ED448_PRIVATE_BYTES] = {1}, sym2[ED448_PRIVATE_BYTES] = {2},
          sym3[ED448_PRIVATE_BYTES] = {3}, sym4[ED448_PRIVATE_BYTES] = {4};
  ec_scalar_t privs[3];
  ec_point pubs[3];
  uint8_t m[64] = {0x01, 0x02, 0x03};
  uint8_t m2[64] = {0x03, 0x02, 0x01};
  ecdh_proof_s res;

  otrng_keypair_generate(&v1, sym1);
  otrng_keypair_generate(&v2, sym2);
  otrng_keypair_generate(&v3, sym3);
  otrng_keypair_generate(&v4, sym4);

  goldilocks_448_scalar_copy(privs[0], v1.priv);
  goldilocks_448_scalar_copy(privs[1], v2.priv);
  goldilocks_448_scalar_copy(privs[2], v3.priv);
  goldilocks_448_point_copy(pubs[0], v1.pub);
  goldilocks_448_point_copy(pubs[1], v2.pub);
  goldilocks_448_point_copy(pubs[2], v3.pub);

  otrng_assert_is_success(otrng_ecdh_proof_generate(
      &res, (const ec_scalar_t *)privs, (const ec_point *)pubs, 3, m, 0x13));
  otrng_assert(
      otrng_ecdh_proof_verify(&res, (const ec_point *)pubs, 3, m, 0x13));
  otrng_assert(
      !otrng_ecdh_proof_verify(&res, (const ec_point *)pubs, 3, m, 0x14));
  otrng_assert(
      !otrng_ecdh_proof_verify(&res, (const ec_point *)pubs, 3, m2, 0x13));

  goldilocks_448_point_copy(pubs[1], v4.pub);

  otrng_assert(
      !otrng_ecdh_proof_verify(&res, (const ec_point *)pubs, 3, m, 0x13));
}

static void *fixed_random_number_generator(size_t n) {
  uint8_t *buf = otrng_xmalloc_z(n);
  buf[0] = 0x01;
  buf[1] = 0x02;
  buf[2] = 0x01;
  buf[3] = 0x04;
  buf[4] = 0x01;
  buf[5] = 0x08;
  return (void *)buf;
}

static void test_dh_proof_generation_and_validation(void) {
  dh_keypair_s v1, v2, v3, v4;
  gcry_mpi_t privs[3];
  gcry_mpi_t pubs[3];
  uint8_t m[64] = {0x01, 0x02, 0x03};
  uint8_t m2[64] = {0x03, 0x02, 0x01};
  dh_proof_s res;

  otrng_dh_keypair_generate(&v1);
  otrng_dh_keypair_generate(&v2);
  otrng_dh_keypair_generate(&v3);
  otrng_dh_keypair_generate(&v4);

  privs[0] = otrng_dh_mpi_copy(v1.priv);
  privs[1] = otrng_dh_mpi_copy(v2.priv);
  privs[2] = otrng_dh_mpi_copy(v3.priv);
  pubs[0] = otrng_dh_mpi_copy(v1.pub);
  pubs[1] = otrng_dh_mpi_copy(v2.pub);
  pubs[2] = otrng_dh_mpi_copy(v3.pub);

  otrng_assert_is_success(
      otrng_dh_proof_generate(&res, (const gcry_mpi_t *)privs,
                              (const gcry_mpi_t *)pubs, 3, m, 0x13, NULL));

  otrng_assert(
      otrng_dh_proof_verify(&res, (const gcry_mpi_t *)pubs, 3, m, 0x13));
  otrng_assert(
      !otrng_dh_proof_verify(&res, (const gcry_mpi_t *)pubs, 3, m, 0x14));
  otrng_assert(
      !otrng_dh_proof_verify(&res, (const gcry_mpi_t *)pubs, 3, m2, 0x13));

  otrng_dh_mpi_release(pubs[1]);
  pubs[1] = otrng_dh_mpi_copy(v4.pub);

  otrng_assert(
      !otrng_dh_proof_verify(&res, (const gcry_mpi_t *)pubs, 3, m, 0x13));

  otrng_dh_keypair_destroy(&v1);
  otrng_dh_keypair_destroy(&v2);
  otrng_dh_keypair_destroy(&v3);
  otrng_dh_keypair_destroy(&v4);

  otrng_dh_mpi_release(privs[0]);
  otrng_dh_mpi_release(privs[1]);
  otrng_dh_mpi_release(privs[2]);
  otrng_dh_mpi_release(pubs[0]);
  otrng_dh_mpi_release(pubs[1]);
  otrng_dh_mpi_release(pubs[2]);
  otrng_dh_mpi_release(res.v);
}

static void test_dh_proof_generation_and_validation_specific_values(void) {
  gcry_mpi_t v1, v2, v3;
  uint8_t v1data[DH_KEY_SIZE] = {0x00, 0x01, 0x42};
  uint8_t v2data[DH_KEY_SIZE] = {0x22, 0x01, 0x42};
  uint8_t v3data[DH_KEY_SIZE] = {0x66, 0x01, 0x42};
  gcry_mpi_t privs[3];
  gcry_mpi_t pubs[3];
  uint8_t m[64] = {0x01, 0x02, 0x03};
  dh_proof_s res;

  gcry_mpi_scan(&v1, GCRYMPI_FMT_USG, v1data, DH_KEY_SIZE, NULL);
  gcry_mpi_scan(&v2, GCRYMPI_FMT_USG, v2data, DH_KEY_SIZE, NULL);
  gcry_mpi_scan(&v3, GCRYMPI_FMT_USG, v3data, DH_KEY_SIZE, NULL);

  privs[0] = v1;
  privs[1] = v2;
  privs[2] = v3;
  pubs[0] = gcry_mpi_new(DH3072_MOD_LEN_BITS);
  otrng_dh_calculate_public_key(pubs[0], privs[0]);
  pubs[1] = gcry_mpi_new(DH3072_MOD_LEN_BITS);
  otrng_dh_calculate_public_key(pubs[1], privs[1]);
  pubs[2] = gcry_mpi_new(DH3072_MOD_LEN_BITS);
  otrng_dh_calculate_public_key(pubs[2], privs[2]);

  otrng_assert_is_success(otrng_dh_proof_generate(
      &res, (const gcry_mpi_t *)privs, (const gcry_mpi_t *)pubs, 3, m, 0x14,
      fixed_random_number_generator));

  otrng_assert(
      otrng_dh_proof_verify(&res, (const gcry_mpi_t *)pubs, 3, m, 0x14));

  otrng_dh_mpi_release(privs[0]);
  otrng_dh_mpi_release(privs[1]);
  otrng_dh_mpi_release(privs[2]);
  otrng_dh_mpi_release(pubs[0]);
  otrng_dh_mpi_release(pubs[1]);
  otrng_dh_mpi_release(pubs[2]);
  otrng_dh_mpi_release(res.v);
}

static void test_ecdh_proof_serialization(void) {
  otrng_keypair_s v1;
  uint8_t sym1[ED448_PRIVATE_BYTES] = {1};
  ecdh_proof_s px;
  uint8_t out[64 + ED448_SCALAR_BYTES] = {0};
  size_t written;
  uint8_t expected[64 + ED448_SCALAR_BYTES] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x53, 0x4b, 0x40, 0xab, 0xd6, 0x50, 0x08, 0x1d, 0x77,
      0x53, 0x8b, 0x10, 0x93, 0x79, 0x64, 0x00, 0x41, 0x12, 0x64, 0xb1, 0x2d,
      0x28, 0xf4, 0x5b, 0x6b, 0xfc, 0x47, 0x0e, 0xd3, 0x27, 0xa6, 0x5e, 0x2f,
      0x5f, 0x24, 0xe4, 0xc0, 0x5a, 0x3f, 0x9c, 0xf6, 0x1f, 0x50, 0x55, 0x6e,
      0x4c, 0xd0, 0xa0, 0xe6, 0xf6, 0xe1, 0xf4, 0xe1, 0x2a, 0x29, 0xc6, 0x20,
  };

  memset(px.c, 0, 64);

  otrng_keypair_generate(&v1, sym1);
  goldilocks_448_scalar_copy(px.v, v1.priv);
  px.c[0] = 0x42;
  px.c[63] = 0x53;

  written = otrng_ecdh_proof_serialize(out, &px);
  g_assert_cmpuint(120, ==, written);
  otrng_assert_cmpmem(expected, out, sizeof(expected));
}

static void test_dh_proof_serialization(void) {
  dh_proof_s px;
  uint8_t v1data[DH_KEY_SIZE] = {0x00, 0x01, 0x42};
  uint8_t out[64 + DH_MPI_MAX_BYTES] = {0};
  size_t written;
  uint8_t expected[147] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x4f, 0x01, 0x42, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00,
  };

  memset(px.c, 0, 64);
  px.c[0] = 0x42;
  px.c[63] = 0x53;

  gcry_mpi_scan(&px.v, GCRYMPI_FMT_USG, v1data, DH_KEY_SIZE, NULL);

  written = otrng_dh_proof_serialize(out, &px);
  g_assert_cmpuint(147, ==, written);
  otrng_assert_cmpmem(expected, out, 147);

  otrng_dh_mpi_release(px.v);
}

static void test_ecdh_proof_deserialization(void) {
  otrng_keypair_s v1;
  uint8_t sym1[ED448_PRIVATE_BYTES] = {1};
  uint8_t data[64 + ED448_SCALAR_BYTES + 2] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x53, 0x4b, 0x40, 0xab, 0xd6, 0x50, 0x08, 0x1d, 0x77,
      0x53, 0x8b, 0x10, 0x93, 0x79, 0x64, 0x00, 0x41, 0x12, 0x64, 0xb1, 0x2d,
      0x28, 0xf4, 0x5b, 0x6b, 0xfc, 0x47, 0x0e, 0xd3, 0x27, 0xa6, 0x5e, 0x2f,
      0x5f, 0x24, 0xe4, 0xc0, 0x5a, 0x3f, 0x9c, 0xf6, 0x1f, 0x50, 0x55, 0x6e,
      0x4c, 0xd0, 0xa0, 0xe6, 0xf6, 0xe1, 0xf4, 0xe1, 0x2a, 0x29, 0xc6, 0x20,
      0x00, 0x01,
  };
  uint8_t expected_c[64] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53,
  };
  ecdh_proof_s px;
  size_t read;

  otrng_keypair_generate(&v1, sym1);

  otrng_assert_is_success(otrng_ecdh_proof_deserialize(
      &px, data, 64 + ED448_SCALAR_BYTES + 2, &read));
  g_assert_cmpuint(120, ==, read);
  otrng_assert_cmpmem(expected_c, px.c, 64);
  otrng_assert(otrng_ec_scalar_eq(px.v, v1.priv));
}

static void test_dh_proof_deserialization(void) {
  uint8_t data[149] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x4f, 0x01, 0x42, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x42,
  };
  uint8_t expected_c[64] = {
      0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53,
  };
  dh_proof_s px;
  size_t read;
  dh_mpi_t expected_v;

  uint8_t v1data[DH_KEY_SIZE] = {0x00, 0x01, 0x42};
  gcry_mpi_scan(&expected_v, GCRYMPI_FMT_USG, v1data, DH_KEY_SIZE, NULL);

  otrng_assert_is_success(otrng_dh_proof_deserialize(&px, data, 149, &read));
  g_assert_cmpuint(147, ==, read);
  otrng_assert_cmpmem(expected_c, px.c, 64);
  otrng_assert_dh_public_key_eq(px.v, expected_v);

  otrng_dh_mpi_release(px.v);
  otrng_dh_mpi_release(expected_v);
}

void units_prekey_proofs_add_tests(void) {
  g_test_add_func("/prekey_server/proofs/dh_gen_validation",
                  test_dh_proof_generation_and_validation);
  g_test_add_func("/prekey_server/proofs/ecdh_gen_validation",
                  test_ecdh_proof_generation_and_validation);
  g_test_add_func("/prekey_server/proofs/dh/gen_and_verify/fixed",
                  test_dh_proof_generation_and_validation_specific_values);
  g_test_add_func("/prekey_server/proofs/ecdh/serialization",
                  test_ecdh_proof_serialization);
  g_test_add_func("/prekey_server/proofs/dh/serialization",
                  test_dh_proof_serialization);
  g_test_add_func("/prekey_server/proofs/ecdh/deserialization",
                  test_ecdh_proof_deserialization);
  g_test_add_func("/prekey_server/proofs/dh/deserialization",
                  test_dh_proof_deserialization);
}
