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

#include "deserialize.h"
#include "serialize.h"

static void test_ser_deser_uint() {
  const uint8_t ser[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};

  size_t read = 0;
  uint8_t buf[8] = {0};

  otrng_serialize_uint8(buf, 0x12);
  otrng_assert_cmpmem(buf, ser, 1);

  uint8_t uint8_des = 0;
  otrng_assert_is_success(
      otrng_deserialize_uint8(&uint8_des, ser, sizeof(ser), &read));
  g_assert_cmpuint(uint8_des, ==, 0x12);
  g_assert_cmpint(read, ==, sizeof(uint8_t));

  memset(buf, 0, sizeof buf);
  otrng_serialize_uint16(buf, 0x1234);
  otrng_assert_cmpmem(buf, ser, 2);

  uint16_t uint16_des = 0;
  otrng_assert_is_success(
      otrng_deserialize_uint16(&uint16_des, ser, sizeof(ser), &read));
  g_assert_cmpuint(uint16_des, ==, 0x1234);
  g_assert_cmpint(read, ==, sizeof(uint16_t));

  memset(buf, 0, sizeof buf);
  otrng_serialize_uint32(buf, 0x12345678);
  otrng_assert_cmpmem(buf, ser, 4);

  uint32_t uint32_des = 0;
  otrng_assert_is_success(
      otrng_deserialize_uint32(&uint32_des, ser, sizeof(ser), &read));
  g_assert_cmpuint(uint32_des, ==, 0x12345678);
  g_assert_cmpint(read, ==, sizeof(uint32_t));

  memset(buf, 0, sizeof buf);
  otrng_serialize_uint64(buf, 0x123456789ABCDEF0);
  otrng_assert_cmpmem(buf, ser, 8);

  uint64_t uint64_des = 0;
  otrng_assert_is_success(
      otrng_deserialize_uint64(&uint64_des, ser, sizeof(ser), &read));
  g_assert_cmpuint(uint64_des, ==, 0x123456789ABCDEF0);
  g_assert_cmpint(read, ==, sizeof(uint64_t));
}

static void test_serialize_otrng_deserialize_data() {
  uint8_t src[5] = {1, 2, 3, 4, 5};
  uint8_t *dst = otrng_xmalloc_z(9);
  otrng_assert(dst);
  g_assert_cmpint(9, ==, otrng_serialize_data(dst, src, 5));
  otrng_free(dst);
}

static void test_ser_des_otrng_public_key() {
  otrng_keypair_s keypair;
  otrng_public_key deser;
  uint8_t sym[ED448_PRIVATE_BYTES] = {1};
  otrng_assert_is_success(otrng_keypair_generate(&keypair, sym));

  uint8_t ser[ED448_PUBKEY_BYTES] = {0};
  g_assert_cmpint(otrng_serialize_public_key(ser, keypair.pub), ==,
                  ED448_PUBKEY_BYTES);
  otrng_assert_is_success(
      otrng_deserialize_public_key(deser, ser, ED448_PUBKEY_BYTES, NULL));

  otrng_assert(otrng_ec_point_valid(deser));

  otrng_assert(otrng_ec_point_eq(deser, keypair.pub));
}

static void test_ser_des_otrng_forging_public_key() {
  otrng_keypair_s keypair;
  otrng_public_key deser;
  uint8_t sym[ED448_PRIVATE_BYTES] = {1};
  otrng_assert_is_success(otrng_keypair_generate(&keypair, sym));

  uint8_t ser[ED448_PUBKEY_BYTES] = {0};
  g_assert_cmpint(otrng_serialize_forging_key(ser, keypair.pub), ==,
                  ED448_PUBKEY_BYTES);
  otrng_assert_is_success(
      otrng_deserialize_forging_key(deser, ser, ED448_PUBKEY_BYTES, NULL));

  otrng_assert(otrng_ec_point_valid(deser));

  otrng_assert(otrng_ec_point_eq(deser, keypair.pub));
}

static void test_ser_des_otrng_shared_prekey() {
  otrng_shared_prekey_pair_s *shared_prekey = otrng_shared_prekey_pair_new();
  otrng_shared_prekey_pub deser;
  uint8_t sym[ED448_PRIVATE_BYTES] = {1};
  otrng_assert_is_success(
      otrng_shared_prekey_pair_generate(shared_prekey, sym));

  uint8_t ser[ED448_PUBKEY_BYTES] = {0};
  g_assert_cmpint(otrng_serialize_shared_prekey(ser, shared_prekey->pub), ==,
                  ED448_PUBKEY_BYTES);
  otrng_assert_is_success(
      otrng_deserialize_shared_prekey(deser, ser, ED448_PUBKEY_BYTES, NULL));

  otrng_assert(otrng_ec_point_valid(deser));

  otrng_assert(otrng_ec_point_eq(deser, shared_prekey->pub) == otrng_true);
  otrng_shared_prekey_pair_free(shared_prekey);
}

static void test_serialize_otrng_symmetric_key() {
  otrng_keypair_s keypair;
  uint8_t sym[ED448_PRIVATE_BYTES] = {1};
  otrng_assert_is_success(otrng_keypair_generate(&keypair, sym));

  const char *expected =
      "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAAAAAAA";

  char *buffer = NULL;
  size_t buffer_size = 0;
  otrng_assert_is_success(
      otrng_symmetric_key_serialize(&buffer, &buffer_size, sym));

  g_assert_cmpint(strlen(expected), ==, buffer_size); /* 76 of length*/
  otrng_assert_cmpmem(expected, buffer, buffer_size);

  otrng_secure_free(buffer);
}

static void test_otrng_serialize_dh_public_key() {
  dh_mpi TEST_DH;
  uint8_t dh_data[383] = {
      0x4c, 0x4e, 0x7b, 0xbd, 0x33, 0xd0, 0x9e, 0x63, 0xfd, 0xe4, 0x67, 0xee,
      0x6c, 0x65, 0x47, 0xc4, 0xe2, 0x1f, 0xaa, 0xb1, 0x90, 0x56, 0x8a, 0x7d,
      0x16, 0x7c, 0x3a, 0x0c, 0xb5, 0xcf, 0xdf, 0xbc, 0x05, 0x44, 0xf0, 0x89,
      0x2d, 0xa4, 0x94, 0x97, 0x40, 0x13, 0x83, 0x2d, 0x74, 0x62, 0xfb, 0xee,
      0xec, 0x31, 0xac, 0xc2, 0x60, 0x5b, 0x45, 0x9b, 0xef, 0x10, 0x3d, 0xfb,
      0x49, 0xe6, 0x0f, 0x8e, 0xfb, 0xba, 0x74, 0x53, 0xfe, 0x13, 0x3a, 0x45,
      0x63, 0xe8, 0x68, 0xa1, 0xd1, 0x01, 0x5c, 0x09, 0x48, 0x78, 0xf2, 0x48,
      0x77, 0x27, 0xde, 0xeb, 0x07, 0xfc, 0xe5, 0xe8, 0xe4, 0x7f, 0x4c, 0x9e,
      0x74, 0x4d, 0xe5, 0xcd, 0x93, 0xdc, 0x54, 0x15, 0xd7, 0xba, 0x13, 0xbf,
      0xa4, 0xfc, 0x7d, 0x7c, 0x2a, 0xcf, 0xf4, 0x84, 0xb9, 0x50, 0x74, 0xfe,
      0x4d, 0x8f, 0x85, 0x8e, 0x22, 0xd4, 0x35, 0x49, 0x1c, 0x7f, 0x71, 0x44,
      0xfe, 0x05, 0x72, 0x12, 0x0c, 0x3d, 0x10, 0xeb, 0x60, 0x8c, 0xa6, 0x01,
      0xfb, 0x41, 0x88, 0xff, 0xdd, 0x77, 0xe4, 0x90, 0x23, 0xfd, 0xde, 0x01,
      0xc7, 0x43, 0x43, 0x56, 0x7d, 0x52, 0xfd, 0xeb, 0x79, 0x82, 0x34, 0x86,
      0x6b, 0x3f, 0xd9, 0x9d, 0x7b, 0x5b, 0xb8, 0xc6, 0x69, 0x1a, 0xd4, 0xdd,
      0x47, 0x60, 0x35, 0x5e, 0x66, 0x9c, 0xda, 0xc3, 0x33, 0x66, 0xa3, 0x8d,
      0x14, 0x9a, 0x2d, 0xeb, 0x19, 0x10, 0x1e, 0x69, 0xb7, 0x06, 0xdc, 0xef,
      0x2f, 0xf6, 0x55, 0x37, 0x4d, 0x3a, 0x87, 0x99, 0xd8, 0x55, 0xbb, 0x2c,
      0x1f, 0x5f, 0xa9, 0x1d, 0x87, 0x26, 0x49, 0x0a, 0x27, 0xf4, 0xdc, 0x2f,
      0xf3, 0xd9, 0xb8, 0x5d, 0x84, 0xac, 0xb8, 0x39, 0x91, 0xeb, 0xcd, 0x27,
      0xcd, 0x23, 0x4b, 0xa7, 0x19, 0x77, 0xd1, 0x14, 0xae, 0x04, 0x15, 0x04,
      0xeb, 0x1c, 0x62, 0x48, 0x15, 0xde, 0x28, 0xc1, 0x42, 0x6f, 0x9b, 0xe7,
      0xb6, 0x70, 0xe1, 0xd1, 0x45, 0xb0, 0xb9, 0x6a, 0x1b, 0x5a, 0x21, 0xab,
      0x7e, 0xfe, 0x23, 0xfa, 0x11, 0xf8, 0x99, 0xaf, 0x24, 0xbf, 0xac, 0x24,
      0xcb, 0xa4, 0xd2, 0xef, 0xbb, 0xe8, 0xef, 0x3a, 0x73, 0x45, 0xe3, 0x4e,
      0x9d, 0xaf, 0xcc, 0xe0, 0xbd, 0x11, 0xad, 0x3b, 0xdc, 0xa5, 0xcd, 0x65,
      0x67, 0xd2, 0x66, 0xe9, 0x98, 0x85, 0xcc, 0xbc, 0x19, 0xb9, 0xbf, 0x07,
      0x60, 0xd7, 0x04, 0xa5, 0xc7, 0xff, 0xae, 0x37, 0x5c, 0x83, 0xe2, 0x23,
      0xdd, 0x52, 0x91, 0xf9, 0x20, 0x7b, 0xda, 0xb7, 0x4f, 0x86, 0x4e, 0x1e,
      0x4a, 0xf2, 0xc9, 0x83, 0xe1, 0xa6, 0x59, 0x56, 0xb4, 0x0d, 0xf2, 0xda,
      0xa7, 0xf7, 0xd9, 0x90, 0xc8, 0xcf, 0x53, 0xf2, 0xb7, 0x8a, 0xa8, 0x54,
      0x8a, 0xac, 0xb1, 0xe0, 0x01, 0x8d, 0xc7, 0x3f, 0xac, 0x03, 0x73};
  gcry_error_t err =
      gcry_mpi_scan(&TEST_DH, GCRYMPI_FMT_USG, dh_data, 383, NULL);
  otrng_assert(!err);

  uint8_t dst[DH_MPI_MAX_BYTES] = {0};
  size_t written = 0;
  otrng_assert_is_success(
      otrng_serialize_dh_public_key(dst, DH_MPI_MAX_BYTES, &written, TEST_DH));
  otrng_dh_mpi_release(TEST_DH);
  TEST_DH = NULL;

  g_assert_cmpuint(383 + 4, ==, written);
}

static void test_serializes_fingerprint() {
  otrng_fingerprint expected_fp = {
      0xce, 0x12, 0xf4, 0x21, 0xc2, 0x30, 0x7c, 0x9c, 0x3d, 0x9c, 0x95, 0x09,
      0x1f, 0xe7, 0x15, 0xf5, 0x92, 0x12, 0x57, 0x2f, 0x40, 0x0a, 0xd2, 0xa6,
      0x8c, 0xe5, 0x88, 0x5f, 0xbe, 0xb7, 0xc1, 0x64, 0x71, 0x05, 0xfa, 0x6d,
      0xbc, 0x2d, 0x94, 0x84, 0x39, 0x7a, 0xa2, 0x83, 0xdc, 0xe1, 0xc5, 0x1c,
      0x6d, 0x3e, 0xe3, 0xc0, 0x62, 0x02, 0x62, 0x35,
  };

  uint8_t sym[ED448_PRIVATE_BYTES] = {1};
  otrng_keypair_s keypair;
  otrng_assert_is_success(otrng_keypair_generate(&keypair, sym));

  otrng_fingerprint dst = {0};
  otrng_assert(otrng_serialize_fingerprint(dst, keypair.pub, keypair.pub));

  otrng_assert_cmpmem(expected_fp, dst, sizeof(otrng_fingerprint));
}

// TODO: ADD test for otrng_serialize_ring_sig

void units_serialize_add_tests(void) {
  g_test_add_func("/serialize_and_deserialize/uint", test_ser_deser_uint);
  g_test_add_func("/serialize_and_deserialize/data",
                  test_serialize_otrng_deserialize_data);
  g_test_add_func("/serialize/fingerprint", test_serializes_fingerprint);
  g_test_add_func("/serialize/dh-public-key",
                  test_otrng_serialize_dh_public_key);
  g_test_add_func("/serialize/otrng-symmetric-key",
                  test_serialize_otrng_symmetric_key);
  g_test_add_func("/serialize_and_deserialize/ed448-public-key",
                  test_ser_des_otrng_public_key);
  g_test_add_func("/serialize_and_deserialize/ed448-forging-public-key",
                  test_ser_des_otrng_forging_public_key);
  g_test_add_func("/serialize_and_deserialize/ed448-shared-prekey",
                  test_ser_des_otrng_shared_prekey);
}
