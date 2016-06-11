package(default_visibility = ["//visibility:public"])

filegroup(
  name = "internal_hdrs",
  srcs = glob(["*.h"]),
)

cc_library(
  name = "libgaia_internal",
  srcs = ["pool.cc", "mutex.cc"],
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

cc_test(
  name = "mutex_test",
  srcs = ["mutex_test.cc", "mutex.h"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_internal"],
)

cc_test(
  name = "queue_test",
  srcs = ["queue_test.cc", "queue.h"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_internal"],
)
