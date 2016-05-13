package(default_visibility = ["//visibility:public"])

filegroup(
  name = "internal_hdrs",
  srcs = glob(["*.h"]),
)

cc_library(
  name = "libgaia_internal",
  srcs = ["pool.cc"],
  hdrs = [":internal_hdrs"],
  linkopts = ["-lrt"],
)

cc_test(
  name = "pool_test",
  srcs = ["pool_test.cc", "pool.h"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_internal"],
  linkopts = ["-lrt"],
)
