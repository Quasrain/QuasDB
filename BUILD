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
        "kv/db/log_format.h",
        # "kv/db/log_writer.h",
        # "kv/db/log_writer.cpp",
        "kv/include/cache.h",
        "kv/include/comparator.h",
        "kv/include/filter_policy.h",
        "kv/include/slice.h",
        "kv/include/status.h",
        "kv/util/arena.h",
        "kv/util/arena.cpp",
        "kv/util/bloom.cpp",
        "kv/util/coding.h",
        "kv/util/coding.cpp",
        "kv/util/crc32c.h",
        "kv/util/crc32c.cpp",
        "kv/util/filter_policy.cpp",
        "kv/util/logging.h",
        "kv/util/logging.cpp",
        "kv/util/hash.h",
        "kv/util/hash.cpp",
        "kv/util/no_destructor.h",
        "kv/util/status.cpp"
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
        "@gtest//:gtest_main"
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
        "@gtest//:gtest_main"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "teststatus",
    srcs = [
        "kv/test/status_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testno_destructor",
    srcs = [
        "kv/test/no_destructor_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testlogging",
    srcs = [
        "kv/test/logging_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main"
    ],
)