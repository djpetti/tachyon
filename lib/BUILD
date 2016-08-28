package(default_visibility = ["//visibility:public"])

filegroup(
  name = "libgaia_hdrs",
  srcs = glob(["*.h"]),
)

cc_library(
  name = "libgaia",
  srcs = ["chunk.cc"],
  deps = ["//lib/ipc:libgaia_ipc"],
  hdrs = ["libgaia_hdrs"],
)

cc_test(
  name = "chunk_test",
  srcs = ["chunk_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia", "//lib/ipc:libgaia_ipc"],
)

