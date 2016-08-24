package(default_visibility = ["//visibility:public"])

filegroup(
  name = "ipc_hdrs",
  srcs = glob(["*.h"]),
)

cc_library(
  name = "libgaia_ipc",
  srcs = ["pool.cc", "mutex.cc", "atomics.cc"],
  hdrs = [":ipc_hdrs"],
  linkopts = ["-lrt"],
)

cc_test(
  name = "pool_test",
  srcs = ["pool_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
  linkopts = ["-lrt"],
)

cc_test(
  name = "mutex_test",
  srcs = ["mutex_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
)

cc_test(
  name = "mpsc_queue_test",
  srcs = ["mpsc_queue_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
)

cc_test(
  name = "atomics_test",
  srcs = ["atomics_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
)

cc_test(
  name = "queue_test",
  srcs = ["queue_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
)

cc_test(
  name = "shared_hashmap_test",
  srcs = ["shared_hashmap_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":libgaia_ipc"],
)
