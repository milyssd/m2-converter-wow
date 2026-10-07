"""Safety and binary-preservation checks for the opt-in DBC cleanup tool."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import cleanup_itemdisplayinfo as cleanup


def make_dbc(specs: list[dict]) -> bytes:
    strings = bytearray(b"\0")
    records = []
    for spec in specs:
        fields = [0] * cleanup.FIELD_COUNT
        fields[0] = spec["id"]
        for field, value in spec.get("strings", {}).items():
            fields[field] = len(strings)
            strings.extend(value.encode("latin-1") + b"\0")
        for field, value in spec.get("numbers", {}).items():
            fields[field] = value
        records.append(struct.pack("<25I", *fields))
    return struct.pack(
        "<4s4I", b"WDBC", len(records), cleanup.FIELD_COUNT,
        cleanup.RECORD_SIZE, len(strings),
    ) + b"".join(records) + strings


class BinaryValidationTests(unittest.TestCase):
    def setUp(self):
        self.data = make_dbc([{"id": 42, "strings": {1: "old_shoulder.mdx", 5: "icon", 22: "texture"}}])

    def test_actual_wrath_layout(self):
        row, = cleanup.read_dbc(self.data)
        self.assertEqual(row.model1, "old_shoulder.mdx")
        self.assertEqual(row.fields[0], 42)
        self.assertEqual(len(row.fields), 25)

    def test_wrong_header_or_layout_and_truncated_data(self):
        for bad in (
            b"WDBC",
            b"WDB2" + self.data[4:],
            self.data[:8] + struct.pack("<I", 23) + self.data[12:],
            self.data[:12] + struct.pack("<I", 96) + self.data[16:],
            self.data[:-1],
            self.data + b"unexpected",
        ):
            with self.subTest(data=bad[:20]):
                with self.assertRaises(cleanup.ValidationError):
                    cleanup.read_dbc(bad)

    def test_all_string_offsets_are_validated(self):
        # A corrupt Texture[7] must fail, even though it is not changed.
        bad = bytearray(self.data)
        struct.pack_into("<I", bad, cleanup.HEADER_SIZE + 22 * 4, 0xFFFFFFFF)
        with self.assertRaisesRegex(cleanup.ValidationError, "campo 22"):
            cleanup.read_dbc(bytes(bad))

    def test_unterminated_string_and_nonempty_offset_zero_fail(self):
        bad = self.data[:-1] + b"x"
        with self.assertRaisesRegex(cleanup.ValidationError, "terminador"):
            cleanup.read_dbc(bad)
        bad = bytearray(self.data)
        bad[cleanup.HEADER_SIZE + cleanup.RECORD_SIZE] = 65
        with self.assertRaisesRegex(cleanup.ValidationError, "vacía"):
            cleanup.read_dbc(bytes(bad))

    def test_duplicate_display_ids_fail(self):
        data = make_dbc([{"id": 42}, {"id": 42}])
        with self.assertRaisesRegex(cleanup.ValidationError, "duplicado"):
            cleanup.read_dbc(data)

    def test_only_two_model_fields_change_and_strings_are_preserved(self):
        data = make_dbc([
            {"id": 42, "strings": {1: "shoulder.mdx", 2: "other.mdx", 3: "tex", 6: "icon"},
             "numbers": {7: 123, 10: 456, 23: 789}},
            {"id": 43, "strings": {1: "head.mdx"}},
        ])
        rows = cleanup.read_dbc(data)
        actual = cleanup.apply_cleanup(data, rows[:1])
        expected = bytearray(data)
        expected[24:32] = b"\0" * 8
        self.assertEqual(actual, bytes(expected))
        cleaned = cleanup.read_dbc(actual)
        self.assertEqual((cleaned[0].model1, cleaned[0].model2), ("", ""))
        self.assertEqual(cleaned[1].model1, "head.mdx")


class EligibilityTests(unittest.TestCase):
    def test_supported_armor_only(self):
        types = list(range(29)) + [1000]
        data = make_dbc([{"id": inventory_type + 1, "strings": {1: "dirty.mdx"}} for inventory_type in types])
        mapping = {inventory_type + 1: [cleanup.ItemUse(100 + inventory_type, inventory_type)] for inventory_type in types}
        candidates, skipped = cleanup.plan_cleanup(cleanup.read_dbc(data), mapping)
        self.assertEqual({row.display_id - 1 for row in candidates}, {4, 5, 6, 7, 8, 9, 10, 16, 19, 20})
        self.assertEqual(skipped["protected_slot_or_shared_display"], len(types) - 10)

    def test_display_shared_with_weapon_or_original_attachment_is_protected(self):
        rows = cleanup.read_dbc(make_dbc([{"id": 10, "strings": {1: "weapon.mdx"}}]))
        for protected in (0, 1, 2, 3, 11, 12, 13, 14, 15, 17, 18, 21, 22, 23, 24, 25, 26, 27, 28, 99):
            with self.subTest(inventory_type=protected):
                mapping = {10: [cleanup.ItemUse(1, 5), cleanup.ItemUse(2, protected)]}
                candidates, skipped = cleanup.plan_cleanup(rows, mapping, allow_custom=True)
                self.assertEqual(candidates, [])
                self.assertEqual(skipped["protected_slot_or_shared_display"], 1)

    def test_custom_extension_is_protected_until_explicitly_allowed(self):
        specs = [
            {"id": 1, "strings": {1: "model.mdx", 6: "19"}},
            {"id": 2, "strings": {1: "model.mdx", 6: "::6"}},
            {"id": 3, "strings": {1: "collection.mdx:2301"}},
            {"id": 4, "strings": {2: "collection.mdx:401"}},
            {"id": 5, "strings": {1: "dirty.mdx", 6: "INV_icon"}},
        ]
        rows = cleanup.read_dbc(make_dbc(specs))
        mapping = {i: [cleanup.ItemUse(100 + i, 5)] for i in range(1, 6)}
        candidates, skipped = cleanup.plan_cleanup(rows, mapping)
        self.assertEqual([row.display_id for row in candidates], [5])
        self.assertEqual(skipped["custom_extension"], 4)
        candidates, _ = cleanup.plan_cleanup(rows, mapping, allow_custom=True)
        self.assertEqual(len(candidates), 5)

    def test_unmapped_empty_and_selection_are_preserved(self):
        rows = cleanup.read_dbc(make_dbc([
            {"id": 1, "strings": {1: "unmapped.mdx"}}, {"id": 2},
            {"id": 3, "strings": {1: "chosen.mdx"}},
            {"id": 4, "strings": {1: "not_chosen.mdx"}},
        ]))
        mapping = {i: [cleanup.ItemUse(100 + i, 5)] for i in (2, 3, 4)}
        candidates, skipped = cleanup.plan_cleanup(rows, mapping, only_display_ids={1, 2, 3})
        self.assertEqual([row.display_id for row in candidates], [3])
        self.assertEqual(dict(skipped), {"unmapped": 1, "already_empty": 1, "not_selected": 1})


class CsvAndCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dbc = self.root / "ItemDisplayInfo.dbc"
        self.mapping = self.root / "items.csv"
        self.data = make_dbc([
            {"id": 42, "strings": {1: "dirty_shoulder.mdx", 2: "dirty_other.mdx"}},
            {"id": 43, "strings": {1: "sword.mdx"}},
        ])
        self.dbc.write_bytes(self.data)
        self.mapping.write_text("entry,displayid,InventoryType\n1,42,5\n2,43,13\n", encoding="utf-8")

    def run_cli(self, *extra):
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            status = cleanup.main([str(self.dbc), "--mapping", str(self.mapping), "--json", *map(str, extra)])
        return status, output.getvalue(), errors.getvalue()

    def test_csv_accepts_bom_case_and_all_shared_uses(self):
        self.mapping.write_text("\ufeffENTRY,DisplayID,InventoryType\n1,42,5\n2,42,13\n3,0,0\n", encoding="utf-8")
        mapping = cleanup.read_item_mapping(self.mapping)
        self.assertEqual(mapping[42], [cleanup.ItemUse(1, 5), cleanup.ItemUse(2, 13)])
        self.assertNotIn(0, mapping)

    def test_invalid_csv_fails_before_output(self):
        samples = (
            "entry,displayid\n1,42\n",
            "entry,displayid,InventoryType\n",
            "entry,displayid,InventoryType\n1,42,5\n1,43,13\n",
            "entry,displayid,InventoryType\n1,-1,5\n",
            "entry,displayid,InventoryType\n1,42,\n",
            "entry,displayid,InventoryType\n1,42,5,extra\n",
            "entry,displayid,InventoryType\n1,4294967296,5\n",
            "entry,displayid,InventoryType\n0,42,5\n",
            "entry,displayid,InventoryType,entry\n1,42,5,1\n",
        )
        output_path = self.root / "new.dbc"
        for sample in samples:
            with self.subTest(csv=sample):
                self.mapping.write_text(sample, encoding="utf-8")
                status, _, errors = self.run_cli("--output", output_path)
                self.assertEqual(status, 1)
                self.assertTrue(errors)
                self.assertFalse(output_path.exists())

    def test_default_is_dry_run_with_no_writes(self):
        status, text, errors = self.run_cli()
        self.assertEqual((status, errors), (0, ""))
        report = json.loads(text)
        self.assertEqual(report["mode"], "dry_run")
        self.assertEqual(report["candidate_count"], 1)
        self.assertEqual(report["candidates"][0]["display_id"], 42)
        self.assertEqual(self.dbc.read_bytes(), self.data)
        self.assertEqual({path.name for path in self.root.iterdir()}, {"ItemDisplayInfo.dbc", "items.csv"})

    def test_write_only_new_file_and_refuse_replacement(self):
        output_path = self.root / "clean.dbc"
        status, _, errors = self.run_cli("--output", output_path)
        self.assertEqual((status, errors), (0, ""))
        self.assertEqual(self.dbc.read_bytes(), self.data)
        rows = cleanup.read_dbc(output_path.read_bytes())
        self.assertEqual((rows[0].model1, rows[0].model2), ("", ""))
        self.assertEqual(rows[1].model1, "sword.mdx")
        written = output_path.read_bytes()
        status, _, _ = self.run_cli("--output", output_path)
        self.assertEqual(status, 1)
        self.assertEqual(output_path.read_bytes(), written)
        status, _, _ = self.run_cli("--output", self.dbc)
        self.assertEqual(status, 1)
        self.assertEqual(self.dbc.read_bytes(), self.data)

    def test_existing_symlink_is_never_followed_or_overwritten(self):
        output_path = self.root / "linked.dbc"
        output_path.symlink_to(self.dbc)
        status, _, _ = self.run_cli("--output", output_path)
        self.assertEqual(status, 1)
        self.assertEqual(self.dbc.read_bytes(), self.data)

    def test_missing_or_invalid_selected_id_fails_without_writing(self):
        output_path = self.root / "clean.dbc"
        for selected in (999, -1, 4294967296):
            with self.subTest(display_id=selected):
                status, _, _ = self.run_cli("--only-display-id", selected, "--output", output_path)
                self.assertEqual(status, 1)
                self.assertFalse(output_path.exists())

    def test_invalid_dbc_fails_without_writing(self):
        self.dbc.write_bytes(self.data[:-3])
        output_path = self.root / "clean.dbc"
        status, _, _ = self.run_cli("--output", output_path)
        self.assertEqual(status, 1)
        self.assertFalse(output_path.exists())


if __name__ == "__main__":
    unittest.main()
