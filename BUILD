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
        "kv/db/dbformat.h",
        "kv/db/log_format.h",
        "kv/db/log_reader.h",
        "kv/db/log_reader.cpp",
        "kv/db/log_writer.h",
        "kv/db/log_writer.cpp",
        "kv/db/skiplist.h",
        "kv/include/cache.h",
        "kv/include/comparator.h",
        "kv/include/db.h",
        "kv/include/env.h",
        "kv/include/filter_policy.h",
        "kv/include/iterator.h",
        "kv/include/options.h",
        "kv/include/slice.h",
        "kv/include/status.h",
        "kv/include/table_builder.h",
        "kv/include/table.h",
        "kv/include/write_batch.h",
        "kv/util/arena.h",
        "kv/util/arena.cpp",
        "kv/util/bloom.cpp",
        "kv/util/cache.cpp",
        "kv/util/coding.h",
        "kv/util/coding.cpp",
        "kv/util/crc32c.h",
        "kv/util/crc32c.cpp",
        "kv/util/comparator.cpp",
        "kv/util/env.cpp",
        "kv/util/filter_policy.cpp",
        "kv/util/logging.h",
        "kv/util/logging.cpp",
        "kv/util/mutexlock.h",
        "kv/util/hash.h",
        "kv/util/hash.cpp",
        "kv/util/no_destructor.h",
        "kv/util/random.h",
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

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testcache",
    srcs = [
        "kv/test/cache_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testskiplist",
    srcs = [
        "kv/test/skiplist_test.cpp"
        ],
    deps = [
        ":base",
        "@gtest//:gtest",
        "@gtest//:gtest_main"
    ],
    linkopts = [
        "-std=c++17"
    ]
)