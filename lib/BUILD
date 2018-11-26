package(default_visibility = ["//visibility:public"])

filegroup(
  name = "tachyon_hdrs",
  srcs = glob(["*.h"]),
)

cc_library(
  name = "tachyon",
  srcs = ["pool.cc", "mutex.cc", "atomics.cc"],
  hdrs = [":tachyon_hdrs"],
  linkopts = ["-lrt"],
)

cc_test(
  name = "pool_test",
  srcs = ["pool_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  linkopts = ["-lrt"],
  # This test uses the shared memory. If multiple tests that use it try to run
  # at the same time, it crashes and burns.
  tags = ["exclusive"],
  size = "small",
)

cc_test(
  name = "mutex_test",
  srcs = ["mutex_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  size = "small",
)

cc_test(
  name = "mpsc_queue_test",
  srcs = ["mpsc_queue_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  # This test uses the shared memory.
  tags = ["exclusive"],
  size = "small",
)

cc_test(
  name = "atomics_test",
  srcs = ["atomics_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  size = "small",
)

cc_test(
  name = "queue_test",
  srcs = ["queue_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  # This test uses the shared memory.
  tags = ["exclusive"],
  size = "small",
)

cc_test(
  name = "shared_hashmap_test",
  srcs = ["shared_hashmap_test.cc"],
  copts = ["-Iexternal/gtest/include"],
  deps = ["@gtest//:main", ":tachyon"],
  # This test uses the shared memory.
  tags = ["exclusive"],
  size = "small",
)
