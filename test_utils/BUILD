cc_library(
  name = "test_utils",
  srcs = glob(["*.cc"]),
  hdrs = glob(["*.h"]),
  deps = ["//lib:tachyon",
          "@gtest//:gmock"],
  copts = ["-Iexternal/gtest/googlemock/include"],
  visibility = ["//visibility:public"],
)
