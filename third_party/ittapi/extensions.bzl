"""Module extension that exposes the prebuilt Intel VTune ITT library."""

_VTUNE_SDK = "/opt/intel/oneapi/vtune/2026.0/sdk"

_BUILD_CONTENT = """
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
"""

def _ittapi_repository_impl(ctx):
    sdk = ctx.attr.vtune_sdk_path
    ctx.symlink(sdk + "/include/ittnotify.h", "include/ittnotify.h")
    ctx.symlink(sdk + "/include/libittnotify.h", "include/libittnotify.h")
    ctx.symlink(sdk + "/lib64/libittnotify.a", "lib64/libittnotify.a")
    ctx.file("BUILD.bazel", _BUILD_CONTENT)

_ittapi_repository = repository_rule(
    implementation = _ittapi_repository_impl,
    attrs = {
        "vtune_sdk_path": attr.string(default = _VTUNE_SDK),
    },
    local = True,
)

def _ittapi_dep_impl(_module_ctx):
    _ittapi_repository(name = "ittapi")

ittapi_dep = module_extension(
    implementation = _ittapi_dep_impl,
)
