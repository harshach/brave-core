#!/usr/bin/env python3

# Copyright (c) 2026 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at https://mozilla.org/MPL/2.0/.

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))

import install_origin_adblock_components as installer


class InstallOriginAdBlockComponentsTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.source = Path(self.temp_dir.name) / "source"
        self.destination = Path(self.temp_dir.name) / "destination"

        self.catalog = [
            {
                "uuid": "default",
                "list_text_component": {
                    "component_id": installer.DEFAULT_COMPONENT_ID,
                    "base64_public_key": "default-public-key",
                },
            },
            {
                "uuid": "regional",
                "list_text_component": {
                    "component_id": "a" * 32,
                    "base64_public_key": "regional-public-key",
                },
            },
        ]
        catalog_dir = self.source / installer.CATALOG_COMPONENT_ID
        catalog_dir.mkdir(parents=True)
        (catalog_dir / "list_catalog.json").write_text(
            json.dumps(self.catalog), encoding="utf-8"
        )

        files = installer.component_files(self.source)
        catalog_keys = {
            entry["list_text_component"]["component_id"]:
                entry["list_text_component"]["base64_public_key"]
            for entry in self.catalog
        }
        for component_id, filenames in files.items():
            component_dir = self.source / component_id
            component_dir.mkdir(parents=True, exist_ok=True)
            for filename in filenames:
                (component_dir / filename).write_text("data", encoding="utf-8")
            (component_dir / "manifest.json").write_text(
                json.dumps({
                    "key": catalog_keys.get(component_id, "public-key"),
                    "version": "1.0.0",
                }),
                encoding="utf-8",
            )

        catalog_path = (
            self.source / installer.CATALOG_COMPONENT_ID / "list_catalog.json"
        )
        catalog_path.write_text(json.dumps(self.catalog), encoding="utf-8")

    def test_installs_required_component_files(self):
        installer.install_components(self.source, self.destination)

        for component_id, filenames in installer.component_files(
                self.source).items():
            for filename in filenames:
                self.assertTrue(
                    (self.destination / component_id / filename).is_file()
                )

    def test_rejects_catalog_with_wrong_default_component(self):
        catalog_path = (
            self.source / installer.CATALOG_COMPONENT_ID / "list_catalog.json"
        )
        catalog_path.write_text(
            json.dumps([{
                "uuid": "default",
                "list_text_component": {
                    "component_id": "b" * 32,
                    "base64_public_key": "unexpected-public-key",
                },
            }]),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(ValueError, "unexpected default component"):
            installer.install_components(self.source, self.destination)

    def test_rejects_missing_regional_component(self):
        regional_list = self.source / ("a" * 32) / "list.txt"
        regional_list.unlink()

        with self.assertRaisesRegex(ValueError, "Missing or empty"):
            installer.install_components(self.source, self.destination)

    def test_rejects_mismatched_catalog_public_key(self):
        regional_manifest = self.source / ("a" * 32) / "manifest.json"
        regional_manifest.write_text(
            json.dumps({"key": "wrong-key", "version": "1.0.0"}),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(ValueError, "Public key mismatch"):
            installer.install_components(self.source, self.destination)


if __name__ == "__main__":
    unittest.main()
