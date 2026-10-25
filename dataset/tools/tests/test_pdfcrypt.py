# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the encryption primitives the mutator writes files with: AES-128 and
# AES-256 against the FIPS-197 appendix C vectors, RC4 against its common
# published vector ("Key" / "Plaintext"), CBC padding, and the password entries
# of revisions 3 and 6 read back the way a reader validates the empty password.

from __future__ import annotations

import hashlib
import unittest

import support  # noqa: F401

import pdfcrypt


class CipherTest(unittest.TestCase):
    def test_aes128_fips197(self):
        out = pdfcrypt.aes_ecb(bytes(range(16)), bytes.fromhex("00112233445566778899aabbccddeeff"))
        self.assertEqual(out.hex(), "69c4e0d86a7b0430d8cdb78070b4c55a")

    def test_aes256_fips197(self):
        out = pdfcrypt.aes_ecb(bytes(range(32)), bytes.fromhex("00112233445566778899aabbccddeeff"))
        self.assertEqual(out.hex(), "8ea2b7ca516745bfeafc49904b496089")

    def test_rc4_vector(self):
        self.assertEqual(pdfcrypt.rc4(b"Key", b"Plaintext").hex(), "bbf316e8d940af0ad3")

    def test_cbc_pads_to_whole_blocks(self):
        self.assertEqual(len(pdfcrypt.aes_cbc(bytes(16), bytes(16), b"x" * 16)), 32)
        self.assertEqual(len(pdfcrypt.aes_cbc(bytes(16), bytes(16), b"x" * 5)), 16)


class PasswordTest(unittest.TestCase):
    doc_id = hashlib.md5(b"fixture").digest()

    def entries_r3(self, user: bytes):
        o_entry = pdfcrypt.owner_entry_r234(b"owner", user, 3, 16)
        key = pdfcrypt.file_key_r234(user, o_entry, -4, self.doc_id, 3, 16)
        return o_entry, pdfcrypt.user_entry_r234(key, self.doc_id, 3)

    def empty_password_opens_r3(self, o_entry: bytes, u_entry: bytes) -> bool:
        key = pdfcrypt.file_key_r234(b"", o_entry, -4, self.doc_id, 3, 16)
        return pdfcrypt.user_entry_r234(key, self.doc_id, 3)[:16] == u_entry[:16]

    def test_r3_empty_user_password_validates(self):
        self.assertTrue(self.empty_password_opens_r3(*self.entries_r3(b"")))

    def test_r3_user_password_does_not_validate_empty(self):
        self.assertFalse(self.empty_password_opens_r3(*self.entries_r3(b"secret")))

    def test_r6_entries_validate_only_their_password(self):
        salts = bytes(range(32))
        entries = pdfcrypt.entries_r56(b"", b"owner", bytes(32), 6, -4, True, salts)
        u = entries["U"]
        self.assertEqual(pdfcrypt.hash_r6(b"", u[32:40], b""), u[:32])
        locked = pdfcrypt.entries_r56(b"secret", b"owner", bytes(32), 6, -4, True, salts)["U"]
        self.assertNotEqual(pdfcrypt.hash_r6(b"", locked[32:40], b""), locked[:32])

    def test_object_key_length(self):
        self.assertEqual(len(pdfcrypt.object_key(bytes(5), 12, 0, False)), 10)
        self.assertEqual(len(pdfcrypt.object_key(bytes(16), 12, 0, True)), 16)


if __name__ == "__main__":
    unittest.main()
