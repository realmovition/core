load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "cyber_recorder",
    srcs = ["main.cc"],
    linkopts = ["-pthread"],
    deps = [
        ":info",
        ":recorder",
        ":recoverer",
        ":spliter",
        "//cyber:init",
        "//cyber/common:file",
        "//cyber/common:time_conversion",
        "//cyber/tools/cyber_recorder/player",
    ],
)

cc_library(
    name = "info",
    srcs = ["info.cc"],
    hdrs = ["info.h"],
    deps = [
        "//cyber/common:time_conversion",
        "//cyber/proto:record_cc_proto",
        "//cyber/record:record_file_reader",
        "//cyber/record:record_message",
    ],
)

cc_library(
    name = "recorder",
    srcs = ["recorder.cc"],
    hdrs = ["recorder.h"],
    deps = [
        "//cyber",
        "//cyber/common:log",
        "//cyber/proto:record_cc_proto",
        "//cyber/transport/message:pod_message",
        "@fastdds",
    ],
)

cc_test(
    name = "recorder_test",
    size = "small",
    srcs = ["recorder_test.cc"],
    deps = [
        ":recorder",
        "//cyber",
        "//cyber:init",
        "//cyber/record:header_builder",
        "//cyber/record:record_reader",
        "//cyber/transport/message:pod_message",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "recoverer",
    srcs = ["recoverer.cc"],
    hdrs = ["recoverer.h"],
    deps = [
        "//cyber/base:for_each",
        "//cyber/common:log",
        "//cyber/proto:record_cc_proto",
        "//cyber/record:header_builder",
        "//cyber/record:record_file_reader",
        "//cyber/record:record_writer",
    ],
)

cc_library(
    name = "spliter",
    srcs = ["spliter.cc"],
    hdrs = ["spliter.h"],
    deps = [
        "//cyber/common:log",
        "//cyber/proto:record_cc_proto",
        "//cyber/record:header_builder",
        "//cyber/record:record_file_reader",
        "//cyber/record:record_writer",
    ],
)
