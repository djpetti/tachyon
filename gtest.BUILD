cc_library(
  name = "gtest",
  srcs = glob(
    ["googletest/src/*.cc"],
    exclude = ["googletest/src/gtest-all.cc"]
  ),
  hdrs = glob([
    "googletest/include/**/*.h",
    "googletest/src/*.h"
  ]),
  copts = ["-Iexternal/gtest/googletest/include",
           "-Iexternal/gtest/googletest"],
  linkopts = ["-pthread"],
  visibility = ["//visibility:public"],
)

cc_library(
  name = "gmock",
  srcs = glob(
    ["googlemock/src/*.cc"],
    exclude = ["googlemock/src/gmock-all.cc"]
  ),
  hdrs = glob([
    "googlemock/include/**/*.h",
    "googlemock/src/*.h",
    "googletest/include/**/*.h",
  ]),
  copts = ["-Iexternal/gtest/googlemock/include",
           "-Iexternal/gtest/googletest/include"],
  visibility = ["//visibility:public"],
)
