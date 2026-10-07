#!/usr/bin/env python3
"""Inspect/clean unused stock model fields in Wrath 3.3.5a ItemDisplayInfo.dbc.

No third-party dependencies. Only ModelName[0] and ModelName[1] are changed;
the string block and every other byte remain intact. See tools/README.md.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


# AzerothCore DBCfmt.h: ItemDisplayTemplateEntryfmt =
# "nxxxxsxxxxxxxxxxxxxxxxxxx" (25 fields). WoWDBDefs ItemDisplayInfo.dbd,
# build 3.3.5.12340: ID, ModelName[2], ModelTexture[2], InventoryIcon[2],
# GeosetGroup[3], Flags, SpellVisualID, GroupSoundIndex, HelmetGeosetVisID[2],
# Texture[8], ItemVisual, ParticleColorID.
FIELD_COUNT = 25
RECORD_SIZE = FIELD_COUNT * 4
HEADER_SIZE = 20
STRING_FIELDS = (1, 2, 3, 4, 5, 6, *range(15, 23))
EXTENDED_ARMOR_TYPES = frozenset({4, 5, 6, 7, 8, 9, 10, 16, 19, 20})
UINT32_MAX = 2**32 - 1


class ValidationError(ValueError):
    """Invalid input; no output should be written."""


@dataclass(frozen=True)
class DisplayRow:
    display_id: int
    offset: int
    fields: tuple[int, ...]
    model1: str
    model2: str
    icon2: str


@dataclass(frozen=True)
class ItemUse:
    entry: int
    inventory_type: int


def read_dbc(data: bytes) -> list[DisplayRow]:
    """Read only the original 25-field WDBC layout, validating all strings."""
    if len(data) < HEADER_SIZE:
        raise ValidationError("Cabecera DBC incompleta (se necesitan 20 bytes).")
    magic, count, field_count, record_size, string_size = struct.unpack_from(
        "<4s4I", data
    )
    if magic != b"WDBC":
        raise ValidationError("Se requiere un DBC WDBC de WoW 3.3.5a (12340).")
    if field_count != FIELD_COUNT or record_size != RECORD_SIZE:
        raise ValidationError(
            f"Formato incompatible: {field_count} campos/{record_size} bytes; "
            f"se requieren {FIELD_COUNT} campos/{RECORD_SIZE} bytes."
        )
    string_start = HEADER_SIZE + count * record_size
    expected_size = string_start + string_size
    if len(data) != expected_size:
        raise ValidationError(
            f"Tamaño DBC incorrecto: cabecera indica {expected_size} bytes, "
            f"archivo tiene {len(data)}."
        )
    strings = data[string_start:]
    if not strings or strings[0] != 0:
        raise ValidationError("El bloque de cadenas debe comenzar con la cadena vacía NUL.")
    # Reused string offsets are common. Validate/decode each offset only once.
    decoded: dict[int, str] = {}

    def get_string(string_offset: int, display_id: int, field: int) -> str:
        if string_offset in decoded:
            return decoded[string_offset]
        if string_offset >= string_size:
            raise ValidationError(
                f"Display {display_id}, campo {field}: offset de cadena fuera del bloque."
            )
        end = strings.find(b"\0", string_offset)
        if end == -1:
            raise ValidationError(
                f"Display {display_id}, campo {field}: cadena sin terminador NUL."
            )
        # Paths are normally ASCII. Latin-1 preserves any original byte without
        # imposing a locale or altering the original string block.
        value = strings[string_offset:end].decode("latin-1")
        decoded[string_offset] = value
        return value

    rows = []
    seen_ids: set[int] = set()
    for index in range(count):
        offset = HEADER_SIZE + index * record_size
        fields = struct.unpack_from("<25I", data, offset)
        display_id = fields[0]
        if display_id in seen_ids:
            raise ValidationError(f"ID de display duplicado en el DBC: {display_id}.")
        seen_ids.add(display_id)
        values = {
            field: get_string(fields[field], display_id, field)
            for field in STRING_FIELDS
        }
        rows.append(DisplayRow(display_id, offset, fields, values[1], values[2], values[6]))
    return rows


def _number(value: str | None, label: str, line: int) -> int:
    if value is None or not re.fullmatch(r"[0-9]+", value.strip()):
        raise ValidationError(f"CSV línea {line}: {label} debe ser un entero sin signo.")
    number = int(value.strip())
    if number > UINT32_MAX:
        raise ValidationError(f"CSV línea {line}: {label} supera uint32.")
    return number


def read_item_mapping(path: Path) -> dict[int, list[ItemUse]]:
    """Read a complete CSV export of item_template; never infer slot from DBC."""
    mapping: dict[int, list[ItemUse]] = defaultdict(list)
    seen_entries: set[int] = set()
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None:
            raise ValidationError("El CSV está vacío; se requiere la cabecera.")
        normalized = [name.strip().lower() for name in reader.fieldnames]
        if len(set(normalized)) != len(normalized):
            raise ValidationError("El CSV tiene columnas duplicadas.")
        names = dict(zip(normalized, reader.fieldnames))
        required = {"entry", "displayid", "inventorytype"}
        if not required.issubset(names):
            raise ValidationError("Cabecera CSV requerida: entry,displayid,InventoryType.")
        for line, row in enumerate(reader, start=2):
            if None in row:
                raise ValidationError(f"CSV línea {line}: hay más valores que columnas.")
            entry = _number(row[names["entry"]], "entry", line)
            display_id = _number(row[names["displayid"]], "displayid", line)
            inventory_type = _number(row[names["inventorytype"]], "InventoryType", line)
            if entry == 0:
                raise ValidationError(f"CSV línea {line}: entry no puede ser cero.")
            if entry in seen_entries:
                raise ValidationError(f"CSV línea {line}: entry duplicado: {entry}.")
            seen_entries.add(entry)
            # Unknown future types are deliberately retained and protected by
            # the eligibility check, rather than assumed to be harmless armor.
            if display_id:
                mapping[display_id].append(ItemUse(entry, inventory_type))
    if not seen_entries:
        raise ValidationError("El CSV no contiene objetos de item_template.")
    return dict(mapping)


def plan_cleanup(
    rows: Iterable[DisplayRow],
    mapping: dict[int, list[ItemUse]],
    *,
    allow_custom: bool = False,
    only_display_ids: set[int] | None = None,
) -> tuple[list[DisplayRow], Counter[str]]:
    candidates = []
    skipped: Counter[str] = Counter()
    for row in rows:
        if only_display_ids is not None and row.display_id not in only_display_ids:
            skipped["not_selected"] += 1
            continue
        uses = mapping.get(row.display_id)
        if not uses:
            skipped["unmapped"] += 1
            continue
        if any(use.inventory_type not in EXTENDED_ARMOR_TYPES for use in uses):
            skipped["protected_slot_or_shared_display"] += 1
            continue
        if not row.model1 and not row.model2:
            skipped["already_empty"] += 1
            continue
        custom = (
            row.icon2.startswith(":")
            or (bool(row.icon2) and "0" <= row.icon2[0] <= "9")
            or ":" in row.model1
            or ":" in row.model2
        )
        if custom and not allow_custom:
            skipped["custom_extension"] += 1
            continue
        candidates.append(row)
    return candidates, skipped


def apply_cleanup(data: bytes, candidates: Iterable[DisplayRow]) -> bytes:
    result = bytearray(data)
    for row in candidates:
        struct.pack_into("<2I", result, row.offset + 4, 0, 0)
    return bytes(result)


def write_new_file(path: Path, data: bytes) -> None:
    """Exclusive creation prevents replacement of an original or prior output."""
    # Opening with xb also protects existing symlinks and races with another
    # invocation. Do not truncate files or silently replace earlier outputs.
    with path.open("xb") as output:
        try:
            output.write(data)
        except BaseException:
            path.unlink(missing_ok=True)
            raise


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Inspecciona ModelName1/2 de armaduras WoW 3.3.5a para WarcraftXL. "
            "Sin --output solo muestra un informe; nunca modifica el original."
        ),
        epilog=(
            "Exporte TODOS los objetos: SELECT entry, displayid, InventoryType "
            "FROM item_template; y guárdelos como CSV con cabecera. Un export "
            "parcial puede ocultar usos compartidos. Revise las filas candidatas: "
            "los modelos personalizados sin Icon2/':' no se pueden reconocer. "
            "Documentación: tools/README.md."
        ),
    )
    parser.add_argument("dbc", type=Path, help="ItemDisplayInfo.dbc del cliente 3.3.5a (12340)")
    parser.add_argument("--mapping", required=True, type=Path, help="CSV completo de item_template")
    parser.add_argument("--output", type=Path, help="crea un DBC nuevo; el destino no debe existir")
    parser.add_argument(
        "--only-display-id", action="append", type=int, dest="display_ids",
        help="limita la limpieza a este ID revisado (puede repetirse)",
    )
    parser.add_argument(
        "--allow-custom", action="store_true",
        help="permite borrar modelos con Icon2 configurado o ':'; requiere revisión manual",
    )
    parser.add_argument("--json", action="store_true", help="imprime el informe como JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        if args.display_ids is not None and any(
            display_id < 0 or display_id > UINT32_MAX for display_id in args.display_ids
        ):
            raise ValidationError("--only-display-id debe estar entre 0 y 4294967295.")
        if args.output is not None:
            if args.output.resolve() == args.dbc.resolve():
                raise ValidationError("El destino debe ser distinto del DBC original.")
            if args.output.exists() or args.output.is_symlink():
                raise ValidationError("El destino ya existe; no se sobrescribirá.")
            if args.output.resolve() == args.mapping.resolve():
                raise ValidationError("El destino debe ser distinto del CSV de objetos.")
        data = args.dbc.read_bytes()
        rows = read_dbc(data)
        mapping = read_item_mapping(args.mapping)
        selected = set(args.display_ids) if args.display_ids is not None else None
        if selected is not None:
            absent = selected - {row.display_id for row in rows}
            if absent:
                raise ValidationError(f"IDs seleccionados ausentes del DBC: {sorted(absent)}.")
        candidates, skipped = plan_cleanup(
            rows, mapping, allow_custom=args.allow_custom, only_display_ids=selected
        )
        report = {
            "mode": "write" if args.output is not None else "dry_run",
            "rows": len(rows),
            "candidate_count": len(candidates),
            "skipped": dict(sorted(skipped.items())),
            "candidates": [
                {
                    "display_id": row.display_id,
                    "item_entries": [use.entry for use in mapping[row.display_id]],
                    "inventory_types": sorted({use.inventory_type for use in mapping[row.display_id]}),
                    "model1": row.model1,
                    "model2": row.model2,
                    "icon2": row.icon2,
                }
                for row in candidates
            ],
            "output": str(args.output) if args.output is not None else None,
        }
        if args.output is not None:
            write_new_file(args.output, apply_cleanup(data, candidates))
        if args.json:
            print(json.dumps(report, ensure_ascii=True, indent=2))
        else:
            print(f"Filas: {len(rows)}; candidatas: {len(candidates)}; omitidas: {sum(skipped.values())}.")
            for row in candidates:
                print(f"  Display {row.display_id}: ModelName1={row.model1!r}, ModelName2={row.model2!r}")
            print("Omitidas: " + (", ".join(f"{key}={value}" for key, value in sorted(skipped.items())) or "ninguna"))
            if args.output is None:
                print("Simulación: no se escribió ningún archivo. Revise antes de usar --output.")
            else:
                print(f"Nuevo DBC: {args.output}. Original conservado.")
        return 0
    except (ValidationError, OSError, UnicodeError, csv.Error) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
