load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "benchmark_monitor",
    srcs = ["cyber_rt_benchmark_suite.cc"],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/proto:unit_test_cc_proto",
        "//cyber/transport/message:pod_message",
    ],
)

cc_binary(
    name = "benchmark_pub",
    srcs = [
        "benchmark_process_common.h",
        "benchmark_pub.cc",
    ],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/proto:unit_test_cc_proto",
        "//cyber/transport/message:pod_message",
    ],
)

cc_binary(
    name = "benchmark_sub",
    srcs = [
        "benchmark_process_common.h",
        "benchmark_sub.cc",
    ],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/proto:unit_test_cc_proto",
        "//cyber/transport/message:pod_message",
    ],
)

cc_binary(
    name = "cyber_rt_benchmark_suite",
    srcs = ["cyber_rt_benchmark_suite.cc"],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/proto:unit_test_cc_proto",
        "//cyber/transport/message:pod_message",
    ],
)
