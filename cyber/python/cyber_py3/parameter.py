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

from ._internal import _CYBER


def _is_finalizing():
    return getattr(sys, "is_finalizing", lambda: False)()


def _unwrap_parameter(param):
    if isinstance(param, Parameter):
        return param.param
    return param


class Parameter(object):
    """Class for Parameter wrapper."""

    def __init__(self, name=None, value=None):
        self.param = None
        if isinstance(name, _CYBER.Parameter) and value is None:
            self.param = name
        elif name is None and value is None:
            self.param = _CYBER.Parameter()
        elif value is None:
            self.param = _CYBER.Parameter(name)
        elif isinstance(value, bool):
            self.param = _CYBER.Parameter(name, value)
        elif isinstance(value, int):
            self.param = _CYBER.Parameter(name, int(value))
        elif isinstance(value, float):
            self.param = _CYBER.Parameter(name, float(value))
        elif isinstance(value, str):
            self.param = _CYBER.Parameter(name, value)
        else:
            raise TypeError("type is not supported: %s" % type(value))

    def close(self):
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def type(self):
        return self.param.type()

    def type_name(self):
        return self.param.type_name()

    def descriptor(self):
        return self.param.descriptor()

    def name(self):
        return self.param.name()

    def debug_string(self):
        return self.param.debug_string()

    def as_string(self):
        return self.param.as_string()

    def as_double(self):
        return self.param.as_double()

    def as_int64(self):
        return self.param.as_int64()

    def as_bool(self):
        return self.param.as_bool()


class ParameterClient(object):
    """Class for ParameterClient wrapper."""

    def __init__(self, node, server_node_name):
        self.param_clt = _CYBER.ParameterClient(node._node, server_node_name)
        self._closed = False
        self._owner = node
        node._register_owned_resource(self)

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def close(self):
        if self._closed or self.param_clt is None:
            return
        self.param_clt.close()
        self.param_clt = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def set_parameter(self, param):
        return self.param_clt.set_parameter(_unwrap_parameter(param))

    def get_parameter(self, param_name):
        return Parameter(self.param_clt.get_parameter(param_name))

    def get_paramslist(self):
        return [Parameter(param) for param in self.param_clt.get_parameter_list()]


class ParameterServer(object):
    """Class for ParameterServer wrapper."""

    def __init__(self, node):
        self.param_srv = _CYBER.ParameterServer(node._node)
        self._closed = False
        self._owner = node
        node._register_owned_resource(self)

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def close(self):
        if self._closed or self.param_srv is None:
            return
        self.param_srv.close()
        self.param_srv = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def set_parameter(self, param):
        return self.param_srv.set_parameter(_unwrap_parameter(param))

    def get_parameter(self, param_name):
        return Parameter(self.param_srv.get_parameter(param_name))

    def get_paramslist(self):
        return [Parameter(param) for param in self.param_srv.get_parameter_list()]
