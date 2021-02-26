cc_library(
    name = "snappy",
    srcs = [
        "kv/lib/libsnappy.a",
        "kv/lib/snappy.h",
        "kv/lib/snappy-stubs-public.h",
    ]
)

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
        "kv/db/builder.h",
        "kv/db/builder.cpp",
        "kv/db/db_impl.h",
        "kv/db/db_impl.cpp",
        "kv/db/db_iter.h",
        "kv/db/db_iter.cpp",
        "kv/db/dbformat.h",
        "kv/db/dbformat.cpp",
        "kv/db/dumpfile.cpp",
        "kv/db/filename.h",
        "kv/db/filename.cpp",
        "kv/db/log_format.h",
        "kv/db/log_reader.h",
        "kv/db/log_reader.cpp",
        "kv/db/log_writer.h",
        "kv/db/log_writer.cpp",
        "kv/db/memtable.h",
        "kv/db/memtable.cpp",
        "kv/db/skiplist.h",
        "kv/db/snapshot.h",
        "kv/db/table_cache.h",
        "kv/db/table_cache.cpp",
        "kv/db/version_edit.h",
        "kv/db/version_edit.cpp",
        "kv/db/version_set.h",
        "kv/db/version_set.cpp",
        "kv/db/write_batch.cpp",
        "kv/db/write_batch_internal.h",
        "kv/include/cache.h",
        "kv/include/comparator.h",
        "kv/include/db.h",
        "kv/include/dumpfile.h",
        "kv/include/env.h",
        "kv/include/filter_policy.h",
        "kv/include/iterator.h",
        "kv/include/options.h",
        "kv/include/slice.h",
        "kv/include/status.h",
        "kv/include/table_builder.h",
        "kv/include/table.h",
        "kv/include/write_batch.h",
        "kv/table/block_builder.h",
        "kv/table/block_builder.cpp",
        "kv/table/block.h",
        "kv/table/block.cpp",
        "kv/table/filter_block.h",
        "kv/table/filter_block.cpp",
        "kv/table/format.h",
        "kv/table/format.cpp",
        "kv/table/iterator_wrapper.h",
        "kv/table/iterator.cpp",
        "kv/table/merger.h",
        "kv/table/merger.cpp",
        "kv/table/table_builder.cpp",
        "kv/table/table.cpp",
        "kv/table/two_level_iterator.h",
        "kv/table/two_level_iterator.cpp",
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
        "kv/util/env_posix.cpp",
        "kv/util/filter_policy.cpp",
        "kv/util/logging.h",
        "kv/util/logging.cpp",
        "kv/util/mutexlock.h",
        "kv/util/hash.h",
        "kv/util/hash.cpp",
        "kv/util/no_destructor.h",
        "kv/util/posix_logger.h",
        "kv/util/random.h",
        "kv/util/status.cpp",
        "kv/util/testhelper.h"
    ],
    linkopts = [
        "-std=c++17",
        "-lboost_system",
        "-lboost_filesystem"
    ],
    deps = [
        ":snappy"
    ]
)

cc_binary(
<<<<<<< HEAD
    name = "demo",
    srcs = [
        "demo.cpp",
=======
    name = "tryuse",
    srcs = [
        "tryuse.cpp",
>>>>>>> dc6b3f25138b8e676e8eb7b438fac6208ab5ad20
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testcrc",
    srcs = [
        "kv/test/crc32c_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testhash",
    srcs = [
        "kv/test/hash_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "teststatus",
    srcs = [
        "kv/test/status_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testno_destructor",
    srcs = [
        "kv/test/no_destructor_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testlogging",
    srcs = [
        "kv/test/logging_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testcache",
    srcs = [
        "kv/test/cache_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testskiplist",
    srcs = [
        "kv/test/skiplist_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread",
        "-std=c++17"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testfilename",
    srcs = [
        "kv/test/filename_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testdbformat",
    srcs = [
        "kv/test/dbformat_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testwrite_batch",
    srcs = [
        "kv/test/write_batch_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testfilter_block",
    srcs = [
        "kv/test/filter_block_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "testtable",
    srcs = [
        "kv/test/table_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

cc_binary(
    name = "testenv",
    srcs = [
        "kv/test/env_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a",
        "kv/lib/libgmock.a",
        "kv/lib/libgmock_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

cc_binary(
    name = "testversion_set",
    srcs = [
        "kv/test/version_set_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

cc_binary(
    name = "testversion_edit",
    srcs = [
        "kv/test/version_edit_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

cc_binary(
    name = "testrecovery",
    srcs = [
        "kv/test/recovery_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a",
        "kv/lib/libgmock.a",
        "kv/lib/libgmock_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)

cc_binary(
    name = "testautocompact",
    srcs = [
        "kv/test/autocompact_test.cpp",
        "kv/lib/libgtest.a",
        "kv/lib/libgtest_main.a",
        "kv/lib/libgmock.a",
        "kv/lib/libgmock_main.a"
        ],
    linkopts = [
        "-pthread"
    ],
    deps = [
        ":base"
    ],
)