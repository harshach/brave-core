#!/usr/bin/env python3

# Copyright (c) 2026 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# Only files inside an app bundle change between builds, so the bundle keeps
# the date it was first created, and packaging copies that date into the DMG.
# macOS caches app icons by path and bundle date, so installing over an older
# build could keep showing its icon. Bumps the bundle's date, then the stamp.

import os
import sys


def main():
    bundle, stamp = sys.argv[1:]
    os.utime(bundle)
    with open(stamp, 'w'):
        pass


if __name__ == '__main__':
    main()
