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
"""Module for wrapper cyber record."""

import collections
import sys

from google.protobuf.descriptor_pb2 import FileDescriptorProto

from . import cyber as _cyber_runtime
from ._internal import _CYBER


PyBagMessage = collections.namedtuple(
    "PyBagMessage", "topic message data_type timestamp"
)


def _is_finalizing():
    return getattr(sys, "is_finalizing", lambda: False)()


class RecordReader(object):
    """Class for cyber RecordReader wrapper."""

    def __init__(self, file_name):
        self.record_reader = _CYBER.RecordReader(file_name)
        self._closed = False
        _cyber_runtime._register_resource(self)

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def close(self):
        if self._closed or self.record_reader is None:
            return
        self.record_reader.close()
        self.record_reader = None
        self._closed = True
        _cyber_runtime._unregister_resource(self)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def read_messages(self, start_time=0, end_time=18446744073709551615):
        while True:
            message = self.record_reader.read_message(start_time, end_time)
            if not message.end:
                yield PyBagMessage(
                    message.channel_name,
                    message.data,
                    message.data_type,
                    message.timestamp,
                )
            else:
                break

    def get_messagenumber(self, channel_name):
        return self.record_reader.get_message_number(channel_name)

    def get_messagetype(self, channel_name):
        return self.record_reader.get_message_type(channel_name)

    def get_protodesc(self, channel_name):
        return self.record_reader.get_proto_desc(channel_name)

    def get_headerstring(self):
        return self.record_reader.get_header_string()

    def reset(self):
        return self.record_reader.reset()

    def get_channellist(self):
        return self.record_reader.get_channel_list()


class RecordWriter(object):
    """Class for cyber RecordWriter wrapper."""

    def __init__(self, file_segmentation_size_kb=0,
                 file_segmentation_interval_sec=0):
        self.record_writer = _CYBER.RecordWriter()
        self._closed = False
        _cyber_runtime._register_resource(self)
        self.record_writer.set_size_of_file_segmentation(
            file_segmentation_size_kb
        )
        self.record_writer.set_interval_of_file_segmentation(
            file_segmentation_interval_sec
        )

    def __del__(self):
        if _is_finalizing():
            return
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def open(self, path):
        return self.record_writer.open(path)

    def close(self):
        if self._closed or self.record_writer is None:
            return
        self.record_writer.close()
        self.record_writer = None
        self._closed = True
        _cyber_runtime._unregister_resource(self)

    def write_channel(self, channel_name, type_name, proto_desc):
        return self.record_writer.write_channel(
            channel_name, type_name, proto_desc
        )

    def write_message(self, channel_name, data, time, raw=True):
        if raw:
            return self.record_writer.write_message(
                channel_name, data, time, b""
            )

        file_desc = data.DESCRIPTOR.file
        proto = FileDescriptorProto()
        file_desc.CopyToProto(proto)
        proto.name = file_desc.name
        desc_str = proto.SerializeToString()
        return self.record_writer.write_message(
            channel_name, data.SerializeToString(), time, desc_str
        )

    def set_size_fileseg(self, size_kilobytes):
        return self.record_writer.set_size_of_file_segmentation(size_kilobytes)

    def set_intervaltime_fileseg(self, time_sec):
        return self.record_writer.set_interval_of_file_segmentation(time_sec)

    def get_messagenumber(self, channel_name):
        return self.record_writer.get_message_number(channel_name)

    def get_messagetype(self, channel_name):
        return self.record_writer.get_message_type(channel_name)

    def get_protodesc(self, channel_name):
        return self.record_writer.get_proto_desc(channel_name)
