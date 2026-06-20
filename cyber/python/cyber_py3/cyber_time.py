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

from ._internal import _CYBER


class Duration(object):
    """Class for cyber Duration wrapper."""

    def __init__(self, other):
        if isinstance(other, Duration):
            self._duration = _CYBER.Duration(other.to_nsec())
        elif isinstance(other, int):
            self._duration = _CYBER.Duration(other)
        elif isinstance(other, float):
            self._duration = _CYBER.Duration(other)
        else:
            raise TypeError("Duration expects int, float, or Duration")

    def sleep(self):
        self._duration.sleep()

    def __str__(self):
        return str(self.to_nsec())

    def to_sec(self):
        return self._duration.to_sec()

    def to_nsec(self):
        return self._duration.to_nsec()

    def iszero(self):
        return self._duration.iszero()

    def __add__(self, other):
        return Duration(self.to_nsec() + other.to_nsec())

    def __radd__(self, other):
        return Duration(self.to_nsec() + other.to_nsec())

    def __sub__(self, other):
        return Duration(self.to_nsec() - other.to_nsec())

    def __lt__(self, other):
        return self.to_nsec() < other.to_nsec()

    def __gt__(self, other):
        return self.to_nsec() > other.to_nsec()

    def __le__(self, other):
        return self.to_nsec() <= other.to_nsec()

    def __ge__(self, other):
        return self.to_nsec() >= other.to_nsec()

    def __eq__(self, other):
        return self.to_nsec() == other.to_nsec()

    def __ne__(self, other):
        return self.to_nsec() != other.to_nsec()


class Time(object):
    """Class for cyber time wrapper."""

    def __init__(self, other):
        if isinstance(other, Time):
            self.time = _CYBER.Time(other.to_nsec())
        elif isinstance(other, int):
            self.time = _CYBER.Time(other)
        elif isinstance(other, float):
            self.time = _CYBER.Time(other)
        else:
            raise TypeError("Time expects int, float, or Time")

    @classmethod
    def _from_native(cls, native_time):
        instance = cls.__new__(cls)
        instance.time = native_time
        return instance

    def __str__(self):
        return str(self.to_nsec())

    def iszero(self):
        return self.time.iszero()

    @staticmethod
    def now():
        return Time._from_native(_CYBER.Time.now())

    @staticmethod
    def mono_time():
        return Time._from_native(_CYBER.Time.mono_time())

    def to_sec(self):
        return self.time.to_sec()

    def to_nsec(self):
        return self.time.to_nsec()

    def sleep_until(self, cyber_time):
        if isinstance(cyber_time, Time):
            self.time.sleep_until(cyber_time.to_nsec())
            return None
        return NotImplemented

    def __sub__(self, other):
        if isinstance(other, Time):
            return Duration(self.to_nsec() - other.to_nsec())
        return Time(self.to_nsec() - other.to_nsec())

    def __add__(self, other):
        return Time(self.to_nsec() + other.to_nsec())

    def __radd__(self, other):
        return Time(self.to_nsec() + other.to_nsec())

    def __lt__(self, other):
        return self.to_nsec() < other.to_nsec()

    def __gt__(self, other):
        return self.to_nsec() > other.to_nsec()

    def __le__(self, other):
        return self.to_nsec() <= other.to_nsec()

    def __ge__(self, other):
        return self.to_nsec() >= other.to_nsec()

    def __eq__(self, other):
        return self.to_nsec() == other.to_nsec()

    def __ne__(self, other):
        return self.to_nsec() != other.to_nsec()


class Rate(object):
    """Class for cyber Rate wrapper."""

    def __init__(self, other):
        if isinstance(other, int):
            self._rate = _CYBER.Rate(other)
        elif isinstance(other, float):
            self._rate = _CYBER.Rate(int(1.0 / other))
        elif isinstance(other, Duration):
            self._rate = _CYBER.Rate(other.to_nsec())
        else:
            raise TypeError("Rate expects int, float, or Duration")

    def __str__(self):
        return "cycle_time = %s, exp_cycle_time = %s" % (
            str(self.get_cycle_time()),
            str(self.get_expected_cycle_time()),
        )

    def sleep(self):
        self._rate.sleep()

    def reset(self):
        self._rate.reset()

    def get_cycle_time(self):
        return Duration(self._rate.get_cycle_time())

    def get_expected_cycle_time(self):
        return Duration(self._rate.get_expected_cycle_time())
