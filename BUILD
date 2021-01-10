# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_library
cc_library(
    name = "base",
    srcs = [
        # conf
        "conf/Config.cpp",
        "conf/Config.h",
        # error
        "error/error_all.h",
        "error/error_system.h",
        # kv
        "kv/include/slice.h",
        "kv/include/status.h",
        "kv/db/log_format.h",
        # "kv/db/log_writer.h",
        # "kv/db/log_writer.cpp",
        "kv/util/arena.h",
        "kv/util/arena.cpp",
        "kv/util/hash.h",
        "kv/util/hash.cpp",
        "kv/util/coding.h",
        "kv/util/coding.cpp",
        "kv/util/crc32c.h",
        "kv/util/crc32c.cpp"
    ],
    linkopts = [
        "-std=c++17",
        "-lboost_system",
        "-lboost_filesystem"
    ],
    deps = [],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testcrc",
    srcs = [
        "kv/test/crc32c_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main" # Only if hello_test.cc has no main()
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testhash",
    srcs = [
        "kv/test/hash_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main" # Only if hello_test.cc has no main()
    ],
)
