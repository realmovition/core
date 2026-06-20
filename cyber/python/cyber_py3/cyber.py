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

import atexit
import sys
import threading
import time
import weakref

from google.protobuf.descriptor_pb2 import FileDescriptorProto

from ._internal import _CYBER


_RESOURCE_LOCK = threading.Lock()
_REGISTERED_RESOURCES = []


def _is_finalizing():
    return getattr(sys, "is_finalizing", lambda: False)()


def _register_resource(resource):
    with _RESOURCE_LOCK:
        _REGISTERED_RESOURCES.append(weakref.ref(resource))


def _unregister_resource(resource):
    with _RESOURCE_LOCK:
        _REGISTERED_RESOURCES[:] = [
            ref for ref in _REGISTERED_RESOURCES
            if ref() is not None and ref() is not resource
        ]


def _close_registered_resources():
    with _RESOURCE_LOCK:
        resources = list(_REGISTERED_RESOURCES)
        _REGISTERED_RESOURCES.clear()

    seen = set()
    for resource_ref in reversed(resources):
        resource = resource_ref()
        if resource is None:
            continue
        resource_id = id(resource)
        if resource_id in seen:
            continue
        seen.add(resource_id)
        resource.close()


def init(module_name="cyber_py"):
    """Init cyber environment."""
    return _CYBER.init(module_name)


def ok():
    """Return whether cyber is running."""
    return _CYBER.ok()


def shutdown():
    """Shutdown cyber environment."""
    _close_registered_resources()
    return _CYBER.shutdown()


def _shutdown_at_exit():
    if _CYBER.is_shutdown():
        return
    _close_registered_resources()
    _CYBER._shutdown_for_python_exit()


def is_shutdown():
    """Return whether cyber has been shutdown."""
    return _CYBER.is_shutdown()


def waitforshutdown():
    """Block until cyber shutdown completes."""
    return _CYBER.wait_for_shutdown()


atexit.register(_shutdown_at_exit)


class Writer(object):
    """Class for cyber writer wrapper."""

    def __init__(self, name, writer, data_type):
        self.name = name
        self._writer = writer
        self.data_type = data_type
        self._closed = False

    def close(self):
        if self._closed or self._writer is None:
            return
        self._writer.close()
        self._writer = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def write(self, data):
        if self._closed or self._writer is None:
            raise RuntimeError("writer has been closed")
        return self._writer.write(data.SerializeToString())


class Reader(object):
    """Class for cyber reader wrapper."""

    def __init__(self, name, reader, data_type):
        self.name = name
        self._reader = reader
        self.data_type = data_type
        self._closed = False

    def close(self):
        if self._closed or self._reader is None:
            return
        self._reader.close()
        self._reader = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


class Client(object):
    """Class for cyber service client wrapper."""

    def __init__(self, client, data_type):
        self._client = client
        self.data_type = data_type
        self._closed = False

    def close(self):
        if self._closed or self._client is None:
            return
        self._client.close()
        self._client = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def send_request(self, data):
        if self._closed or self._client is None:
            raise RuntimeError("client has been closed")
        response_str = self._client.send_request(data.SerializeToString())
        if len(response_str) == 0:
            return None

        response = self.data_type()
        response.ParseFromString(response_str)
        return response


class Service(object):
    """Class for cyber service wrapper."""

    def __init__(self, name, service):
        self.name = name
        self._service = service
        self._closed = False

    def close(self):
        if self._closed or self._service is None:
            return
        self._service.close()
        self._service = None
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


class Node(object):
    """Class for cyber Node wrapper."""

    def __init__(self, name):
        self._node = _CYBER.Node(name)
        self.list_writer = []
        self.list_reader = []
        self.list_client = []
        self.list_service = []
        self._owned_resources = []
        self._closed = False
        _register_resource(self)

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def close(self):
        if self._closed or self._node is None:
            return

        self._closed = True
        for resource in reversed(self._owned_resources):
            resource.close()
        for service in self.list_service:
            service.close()
        for client in self.list_client:
            client.close()
        for reader in self.list_reader:
            reader.close()
        for writer in self.list_writer:
            writer.close()
        self._node.close()

        self.list_writer = []
        self.list_reader = []
        self.list_client = []
        self.list_service = []
        self._owned_resources = []
        self._node = None
        _unregister_resource(self)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def register_message(self, file_desc):
        """Register proto message descriptor and its dependencies."""
        for dep in file_desc.dependencies:
            self.register_message(dep)
        proto = FileDescriptorProto()
        file_desc.CopyToProto(proto)
        proto.name = file_desc.name
        self._node.register_message(proto.SerializeToString())

    def _register_owned_resource(self, resource):
        self._owned_resources.append(resource)
        return resource

    def create_writer(self, name, data_type, qos_depth=1):
        """Create a channel writer."""
        self.register_message(data_type.DESCRIPTOR.file)
        datatype = data_type.DESCRIPTOR.full_name
        writer = self._node.create_writer(name, datatype, qos_depth)
        writer_obj = Writer(name, writer, datatype)
        self.list_writer.append(writer_obj)
        return writer_obj

    def create_reader(self, name, data_type, callback, args=None):
        """Create a channel reader."""
        raw_data = data_type == "RawData"
        node_ref = weakref.ref(self)

        def on_message(message):
            node = node_ref()
            if node is None or node._closed:
                return

            if raw_data:
                proto = message
            else:
                proto = data_type()
                proto.ParseFromString(message)

            if args is None:
                callback(proto)
            else:
                callback(proto, args)

        reader = self._node.create_reader(name, raw_data, on_message)
        reader_obj = Reader(name, reader, data_type)
        self.list_reader.append(reader_obj)
        return reader_obj

    def create_rawdata_reader(self, name, callback, args=None):
        """Create RawData reader."""
        return self.create_reader(name, "RawData", callback, args)

    def create_client(self, name, request_data_type, response_data_type):
        """Create a client for a service."""
        self.register_message(request_data_type.DESCRIPTOR.file)
        self.register_message(response_data_type.DESCRIPTOR.file)
        datatype = request_data_type.DESCRIPTOR.full_name
        client = self._node.create_client(name, datatype)
        client_obj = Client(client, response_data_type)
        self.list_client.append(client_obj)
        return client_obj

    def create_service(self, name, req_data_type, res_data_type, callback,
                       args=None):
        """Create a service."""
        self.register_message(req_data_type.DESCRIPTOR.file)
        self.register_message(res_data_type.DESCRIPTOR.file)
        response_type = res_data_type.DESCRIPTOR.full_name
        node_ref = weakref.ref(self)

        def on_request(message):
            node = node_ref()
            if node is None or node._closed:
                return b""

            request = req_data_type()
            request.ParseFromString(message)
            if args is None:
                response = callback(request)
            else:
                response = callback(request, args)
            if response is None:
                return b""
            return response.SerializeToString()

        service = self._node.create_service(name, response_type, on_request)
        service_obj = Service(name, service)
        self.list_service.append(service_obj)
        return service_obj

    def spin(self):
        """Spin until cyber shutdown."""
        while not _CYBER.is_shutdown():
            time.sleep(0.002)


class ChannelUtils(object):

    @staticmethod
    def get_debugstring_rawmsgdata(msg_type, rawmsgdata):
        return _CYBER.ChannelUtils.get_debugstring_rawmsgdata(
            msg_type, rawmsgdata
        )

    @staticmethod
    def get_msgtype(channel_name, sleep_s=2):
        return _CYBER.ChannelUtils.get_msgtype(channel_name, sleep_s)

    @staticmethod
    def get_channels(sleep_s=2):
        return _CYBER.ChannelUtils.get_channels(sleep_s)

    @staticmethod
    def get_channels_info(sleep_s=2):
        return _CYBER.ChannelUtils.get_channels_info(sleep_s)


class NodeUtils(object):

    @staticmethod
    def get_nodes(sleep_s=2):
        return _CYBER.NodeUtils.get_nodes(sleep_s)

    @staticmethod
    def get_node_attr(node_name, sleep_s=2):
        return _CYBER.NodeUtils.get_node_attr(node_name, sleep_s)

    @staticmethod
    def get_readersofnode(node_name, sleep_s=2):
        return _CYBER.NodeUtils.get_readersofnode(node_name, sleep_s)

    @staticmethod
    def get_writersofnode(node_name, sleep_s=2):
        return _CYBER.NodeUtils.get_writersofnode(node_name, sleep_s)


class ServiceUtils(object):

    @staticmethod
    def get_services(sleep_s=2):
        return _CYBER.ServiceUtils.get_services(sleep_s)

    @staticmethod
    def get_service_attr(service_name, sleep_s=2):
        return _CYBER.ServiceUtils.get_service_attr(service_name, sleep_s)
