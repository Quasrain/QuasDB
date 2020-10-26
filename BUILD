# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "main",
    srcs = [
        "main.cpp",
        "write/writer.h",
        "write/writer.cpp"
    ],
    linkopts = [
        "-std=c++17",
        "-lboost_system",
        "-lboost_filesystem"
    ],
    deps = [],
)