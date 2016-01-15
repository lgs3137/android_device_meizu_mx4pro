#
# Copyright (C) 2012 The CyanogenMod Project
# Copyright (C) 2012 The Android Open-Source Project
# Copyright (C) 2013 OmniROM Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""Custom OTA commands for mx4pro"""

import common
import os

TARGET_DIR = os.getenv('OUT')
TARGET_DEVICE = os.getenv('CUSTOM_BUILD')

def FullOTA_InstallEnd(info):
  # Tricks for locked bootloader
  mounted = False
  newscript = []
  for cmd in info.script.script:
    if not ("boot.img" in cmd or "unmount" in cmd):
      if cmd.find("format") == 0:
        newscript.append('run_program("/sbin/sh", "-c", "/sbin/rm -rf /system/*");')
      elif cmd.find("mount") == 0:
        if not mounted:
          newscript.append('run_program("/sbin/mount", "/system");')
          mounted = True
      else:
        newscript.append(cmd)
  info.script.script = newscript
