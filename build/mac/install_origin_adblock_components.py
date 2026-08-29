#!/usr/bin/env python3

# Copyright (c) 2026 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at https://mozilla.org/MPL/2.0/.

"""Installs public Brave Ad Block data as preinstalled macOS components."""

import argparse
import json
from pathlib import Path
import re
import shutil
import sys


CATALOG_COMPONENT_ID = "gkboaolpopklhgplhaaiboijnklogmbc"
DEFAULT_COMPONENT_ID = "iodkpdagapdfkphljnddpjlldadblomo"
RESOURCES_COMPONENT_ID = "mfddibmblmbccpadfndgakiopmmhebop"
COMPONENT_ID_PATTERN = re.compile(r"[a-p]{32}")

CORE_COMPONENT_FILES = {
    CATALOG_COMPONENT_ID: (
        "manifest.json",
        "list_catalog.json",
        "regional_catalog.json",
    ),
    RESOURCES_COMPONENT_ID: ("manifest.json", "resources.json"),
}


def _read_json(path: Path):
    with path.open(encoding="utf-8") as file:
        return json.load(file)


def component_files(source: Path) -> dict[str, tuple[str, ...]]:
    """Returns every component file required by the public filter catalog."""
    catalog = _read_json(
        source / CATALOG_COMPONENT_ID / "list_catalog.json"
    )
    if not isinstance(catalog, list):
        raise ValueError("Ad Block catalog must be a list")

    files = dict(CORE_COMPONENT_FILES)
    list_components = {}
    default_entries = []
    for entry in catalog:
        if not isinstance(entry, dict):
            raise ValueError("Ad Block catalog entries must be objects")
        if entry.get("uuid") == "default":
            default_entries.append(entry)

        component = entry.get("list_text_component")
        if not isinstance(component, dict):
            raise ValueError("Ad Block catalog entry is missing its component")
        component_id = component.get("component_id", "")
        public_key = component.get("base64_public_key", "")
        if not COMPONENT_ID_PATTERN.fullmatch(component_id):
            raise ValueError(
                f"Invalid Ad Block component ID: {component_id!r}"
            )
        if not public_key:
            raise ValueError(
                f"Missing catalog public key for component: {component_id}"
            )
        if component_id in list_components:
            raise ValueError(
                f"Duplicate Ad Block component ID: {component_id}"
            )
        list_components[component_id] = public_key
        files[component_id] = ("manifest.json", "list.txt")

    if len(default_entries) != 1:
        raise ValueError("Ad Block catalog must contain one default list")
    default_component = default_entries[0].get("list_text_component", {})
    if default_component.get("component_id") != DEFAULT_COMPONENT_ID:
        raise ValueError(
            "Ad Block catalog references an unexpected default component"
        )

    return files


def validate_components(source: Path) -> dict[str, tuple[str, ...]]:
    files = component_files(source)
    catalog = _read_json(
        source / CATALOG_COMPONENT_ID / "list_catalog.json"
    )
    catalog_keys = {
        entry["list_text_component"]["component_id"]:
            entry["list_text_component"]["base64_public_key"]
        for entry in catalog
    }

    for component_id, filenames in files.items():
        component_dir = source / component_id
        for filename in filenames:
            path = component_dir / filename
            if not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"Missing or empty component file: {path}")

        manifest = _read_json(component_dir / "manifest.json")
        version = manifest.get("version", "")
        if not re.fullmatch(r"\d+(?:\.\d+)+", version):
            raise ValueError(
                f"Invalid component version for {component_id}: {version!r}"
            )
        if not manifest.get("key"):
            raise ValueError(f"Missing public key for component: {component_id}")
        if (component_id in catalog_keys and
                manifest["key"] != catalog_keys[component_id]):
            raise ValueError(
                f"Public key mismatch for component: {component_id}"
            )

    return files


def install_components(source: Path, destination: Path) -> None:
    files = validate_components(source)
    for component_id, filenames in files.items():
        component_destination = destination / component_id
        component_destination.mkdir(parents=True, exist_ok=True)
        for filename in filenames:
            shutil.copy2(
                source / component_id / filename,
                component_destination / filename,
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--stamp", type=Path)
    args = parser.parse_args()

    install_components(args.source, args.destination)
    if args.stamp:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.touch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
