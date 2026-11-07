# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the panel's answers cache (answers.py) against a member written in
# code, so no reader runs: a record's key holds the input's digest, the member,
# its version, its binaries' digest, the request and the schema; a transient
# failure is asked again while a refusal is kept; records are written whole;
# a changed binary is a new key; an image's samples are kept as a blob named
# by their digest. Run: python -m unittest discover -s tests/panel -p "test_*.py".

from __future__ import annotations

import hashlib
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import answers  # noqa: E402


class FakeMember:
    """A member that answers from a script and counts the questions it was asked."""

    NAME = "fake"
    CAPABILITIES = ("text", "image_pixels")

    def __init__(self, replies, digest="ab" * 32, version="1.0+test"):
        self.replies = list(replies)
        self.digest = digest
        self.release = version
        self.asked = 0

    def version(self):
        return self.release

    def binary_digest(self):
        return self.digest

    def answer(self, path, kind, timeout, request):
        del path, kind, timeout, request
        self.asked += 1
        reply = self.replies.pop(0)
        return {key: (bytes(value) if isinstance(value, bytearray) else value) for key, value in reply.items()}


class AnswersTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        patcher = mock.patch.dict(os.environ, {"PDFKIT_PANEL_RESULTS": str(self.root / "results")})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.folder.cleanup)
        self.input = self.root / "input.bin"
        self.input.write_bytes(b"not a PDF, only bytes to hash")
        self.sha = hashlib.sha256(self.input.read_bytes()).hexdigest()

    def records(self):
        return sorted(p for p in (self.root / "results").rglob("*.json"))

    def test_the_key_holds_every_part(self):
        member = FakeMember([{"ok": True, "pages": [{"text": "a"}]}])
        answers.cached_answer(member, self.input, "text", pages=[1])
        (record,) = self.records()
        relative = record.relative_to(self.root / "results").parts
        self.assertEqual(relative[0], f"fake-1.0+test-{'ab' * 6}")
        self.assertEqual(relative[1], "text")
        self.assertEqual(relative[2], self.sha[:2])
        self.assertEqual(record.name, f"{self.sha}-{answers.request_key('text', {'pages': [1]})}.json")
        self.assertNotEqual(answers.request_key("text", {"pages": [1]}), answers.request_key("text", {"pages": [2]}))
        self.assertNotEqual(answers.request_key("text", {}), answers.request_key("pages", {}))

    def test_the_schema_is_part_of_the_key(self):
        before = answers.request_key("text", {})
        with mock.patch.object(answers, "SCHEMA", answers.SCHEMA + 1):
            self.assertNotEqual(before, answers.request_key("text", {}))

    def test_a_cached_answer_is_read_back_without_asking(self):
        member = FakeMember([{"ok": True, "pages": []}])
        stats = answers.Stats()
        first = answers.cached_answer(member, self.input, "text", stats=stats)
        second = answers.cached_answer(member, self.input, "text", stats=stats)
        self.assertEqual(first, second)
        self.assertEqual(member.asked, 1)
        self.assertEqual((stats.asked, stats.cached), (1, 1))

    def test_a_transient_failure_is_not_kept(self):
        for error in ("timeout", "crash", "worker"):
            member = FakeMember([{"ok": False, "error": error}, {"ok": True, "pages": []}])
            self.assertEqual(answers.cached_answer(member, self.input, "text")["error"], error)
            self.assertEqual(self.records(), [])
            self.assertTrue(answers.cached_answer(member, self.input, "text")["ok"])
            self.assertEqual(member.asked, 2)
            for record in self.records():
                record.unlink()

    def test_a_refusal_is_kept(self):
        member = FakeMember([{"ok": False, "error": "refused", "detail": "Syntax Error"}])
        answers.cached_answer(member, self.input, "text")
        again = answers.cached_answer(member, self.input, "text")
        self.assertEqual(again["error"], "refused")
        self.assertEqual(member.asked, 1)

    def test_a_changed_binary_is_a_new_key(self):
        answers.cached_answer(FakeMember([{"ok": True, "pages": []}]), self.input, "text")
        other = FakeMember([{"ok": True, "pages": []}], digest="cd" * 32)
        answers.cached_answer(other, self.input, "text")
        self.assertEqual(other.asked, 1)
        newer = FakeMember([{"ok": True, "pages": []}], version="1.1")
        answers.cached_answer(newer, self.input, "text")
        self.assertEqual(newer.asked, 1)
        self.assertEqual(len(self.records()), 3)

    def test_a_record_is_written_whole(self):
        target = self.root / "results" / "one" / "record.json"
        answers.write_json(target, {"ok": True})
        answers.write_json(target, {"ok": True, "pages": [1, 2]})
        self.assertEqual(answers.read_json(target), {"ok": True, "pages": [1, 2]})
        self.assertEqual([p.name for p in target.parent.iterdir()], ["record.json"])
        with mock.patch("os.replace", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                answers.write_json(target, {"ok": False})
        self.assertEqual(answers.read_json(target), {"ok": True, "pages": [1, 2]})
        self.assertEqual([p.name for p in target.parent.iterdir()], ["record.json"])

    def test_pixels_are_kept_as_a_blob_named_by_their_digest(self):
        samples = bytes(range(256)) * 3
        member = FakeMember([{"ok": True, "images": {"7": {"width": 16, "height": 16, "layout": "rgb",
                                                           "samples": samples}, "8": {"error": "not_found"}}}])
        answer = answers.cached_answer(member, self.input, "image_pixels", images=[{"object": 7}])
        digest = hashlib.sha256(samples).hexdigest()
        self.assertEqual(answer["images"]["7"]["blob"], digest)
        self.assertNotIn("samples", answer["images"]["7"])
        folder = answers.member_folder("fake", "1.0+test", "ab" * 32)
        self.assertEqual(answers.read_blob(folder, "image_pixels", digest), samples)
        again = answers.cached_answer(member, self.input, "image_pixels", images=[{"object": 7}])
        self.assertEqual(again, answer)
        self.assertIsNone(answers.read_blob(folder, "image_pixels", "00" * 32))

    def test_a_member_with_its_own_cache_is_asked_through_it(self):
        calls = []

        class Elsewhere(FakeMember):
            CACHED_ELSEWHERE = ("text",)

            def cached_elsewhere(self, path, kind, timeout, request, stats):
                calls.append(kind)
                return {"ok": True, "pages": []}

        member = Elsewhere([])
        answers.cached_answer(member, self.input, "text")
        self.assertEqual(calls, ["text"])
        self.assertEqual(self.records(), [])

    def test_a_version_becomes_a_folder_name(self):
        self.assertEqual(answers.safe("chromium/8111"), "chromium-8111")
        self.assertEqual(answers.safe("1.23.10+ds1-1build3"), "1.23.10+ds1-1build3")


if __name__ == "__main__":
    unittest.main()
