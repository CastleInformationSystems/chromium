#!/usr/bin/env python3
# Copyright 2019 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

# Hardcoded Identities provided by user
ID_APP = '92356BF9271EEB209F5784020B6E0519386295EA'
ID_INSTALLER = 'E479F9FC6847702179DCFFC55A38D26758AC43AC'

if __name__ == '__main__':
    # 1. Determine location of this script
    current_script_path = os.path.abspath(__file__)
    current_dir_name = os.path.dirname(current_script_path)
    
    # Add to path to find signing.driver
    sys.path.append(current_dir_name)
    import signing.driver

    # 2. Detect Architecture based on folder path
    # If the script is running from ".../out/Release_ARM64/Chromium Packaging/..."
    if "Release_ARM64" in current_script_path:
        target_input = "out/Release_ARM64"
        target_output = "out/Release_ARM64/signed"
        print(f"✍️  Detected ARM64 Context. Signing {target_input}...")
    else:
        # Default to Intel if not explicitly ARM64
        target_input = "out/Release"
        target_output = "out/Release/signed"
        print(f"✍️  Detected Intel Context. Signing {target_input}...")

    # 3. Construct Arguments
    args = [
        "--input", target_input,
        "--output", target_output,
        "--identity", ID_APP,
        "--installer-identity", ID_INSTALLER,
        "--notarize"
    ]

    # 4. Execute
    signing.driver.main(args)