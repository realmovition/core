load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "examples_test_utils",
    hdrs = ["examples_test_utils.h"],
    deps = ["//cyber:cyber_core"],
)

cc_test(
    name = "examples_integration_test",
    size = "small",
    srcs = ["examples_integration_test.cc"],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/examples/proto:examples_cc_proto",
        ":examples_test_utils",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "examples_stress_test",
    size = "small",
    srcs = ["examples_stress_test.cc"],
    data = ["//cyber:cyber_conf"],
    deps = [
        "//cyber:cyber_core",
        "//cyber/examples/proto:examples_cc_proto",
        ":examples_test_utils",
        "@com_google_googletest//:gtest",
    ],
)

test_suite(
    name = "examples_regression_tests",
    tests = [
        ":examples_integration_test",
        ":examples_stress_test",
    ],
)
