"""Prebuilt ITT (Instrumentation and Tracing Technology) library from Intel VTune."""

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ittapi",
    hdrs = [
        "include/ittnotify.h",
        "include/libittnotify.h",
    ],
    includes = ["include"],
    srcs = ["lib64/libittnotify.a"],
)
