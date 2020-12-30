# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "main",
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
        "kv/db/log_writer.h",
        "kv/db/log_writer.cpp",
        "kv/util/coding.h",
        "kv/util/coding.cpp",
        "kv/util/crc32c.h",
        "kv/util/crc32c.cpp",
        "main.cpp"
    ],
    linkopts = [
        "-std=c++17",
        "-lboost_system",
        "-lboost_filesystem"
    ],
    deps = [],
)