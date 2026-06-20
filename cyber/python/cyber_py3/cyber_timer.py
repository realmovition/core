#!/usr/bin/env python3

# ****************************************************************************
# Copyright 2019 The Apollo Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ****************************************************************************
# -*- coding: utf-8 -*-
"""Module for init environment."""

import sys

from . import cyber as _cyber_runtime
from ._internal import _CYBER


def _is_finalizing():
    return getattr(sys, "is_finalizing", lambda: False)()


class Timer(object):
    """Class for cyber timer wrapper."""

    def __init__(self, period=None, callback=None, oneshot=None):
        self.timer = None
        self._closed = False
        if period is None and callback is None and oneshot is None:
            self.timer = _CYBER.Timer()
        else:
            self.timer = _CYBER.Timer(period, callback, bool(oneshot))
        _cyber_runtime._register_resource(self)

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def close(self):
        if self._closed or self.timer is None:
            return
        self.timer.close()
        self.timer = None
        self._closed = True
        _cyber_runtime._unregister_resource(self)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def set_option(self, period, callback, oneshot=0):
        if self._closed or self.timer is None:
            raise RuntimeError("timer has been closed")
        self.timer.set_option(period, callback, bool(oneshot))

    def start(self):
        if self._closed or self.timer is None:
            raise RuntimeError("timer has been closed")
        self.timer.start()

    def stop(self):
        if self._closed or self.timer is None:
            return
        self.timer.stop()
