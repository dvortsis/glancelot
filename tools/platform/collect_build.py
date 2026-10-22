"""Collect path-free build identity from the actual configured Ninja/Qt toolchain."""
import argparse
import json
import re
import shlex
from pathlib import Path
from artifacts import build_info, encoded, read_limited, require


def cache_values(path):
    result = {}
    for line in read_limited(path, 4 * 1024**2).decode("utf-8").splitlines():
        match = re.fullmatch(r"([^#/:][^:=]*):[^=]+=(.*)", line)
        if match:
            result[match[1]] = match[2]
    return result


def cmake_string(text, name):
    match = re.search(r"^set\(" + re.escape(name) + r' "([^"\r\n]*)"\)', text, re.M)
    require(match is not None, "configured compiler identity missing")
    return match[1]


def collect(repo, build, qt, target):
    repo, build, qt = Path(repo).resolve(), Path(build).resolve(), Path(qt).resolve()
    cache = cache_values(build / "CMakeCache.txt")
    require(Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() == repo, "build belongs to a different source tree")
    require(cache["CMAKE_BUILD_TYPE"] == "Release" and cache["CMAKE_GENERATOR"] == "Ninja", "expected configured Release Ninja build")
    require(Path(cache["Qt6Core_DIR"]).resolve().is_relative_to(qt), "configured Qt differs from supplied Qt")
    cmake = ".".join(cache["CMAKE_CACHE_" + part + "_VERSION"] for part in ("MAJOR", "MINOR", "PATCH"))
    compiler = read_limited(build / "CMakeFiles" / cmake / "CMakeCXXCompiler.cmake", 1024**2).decode("utf-8")
    require(cmake_string(compiler, "CMAKE_CXX_SIZEOF_DATA_PTR") == "8", "package requires a 64-bit compiler")
    if target == "windows-x86_64":
        require(cmake_string(compiler, "CMAKE_CXX_COMPILER_ARCHITECTURE_ID") == "x64", "package requires x64 MSVC")
    else:
        require("x86_64" in cmake_string(compiler, "CMAKE_CXX_LIBRARY_ARCHITECTURE"), "package requires x86_64 Linux")
    qt_version = read_limited(qt / "lib/cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake", 1024**2).decode("utf-8")
    version = re.search(r'^set\(PACKAGE_VERSION "([^"]+)"\)', qt_version, re.M)
    require(version is not None and version[1] == "6.8.3", "configured Qt is not pinned 6.8.3")
    lines = read_limited(build / "build.ninja", 32 * 1024**2).decode("utf-8").splitlines()
    flags = None
    for index, line in enumerate(lines):
        if line.startswith("build ") and "glancelot.dir/app/main.cpp." in line.replace("\\", "/"):
            flags = next((row.split("=", 1)[1].strip() for row in lines[index + 1:index + 12] if row.startswith("  FLAGS =")), None)
            break
    require(flags is not None, "actual viewer compile flags absent from Ninja build")
    tokens = shlex.split(flags)
    require(any(flag in tokens for flag in ("-std:c++20", "/std:c++20", "-std=c++20")), "actual viewer is not configured as C++20")
    source = read_limited(repo / "app/main.cpp", 1024**2).decode("utf-8")
    app_version = re.search(r'setApplicationVersion\("([^"]+)"\)', source)
    require(app_version is not None, "application source version missing")
    return build_info({"app_version": app_version[1], "qt_version": version[1],
        "compiler_id": cmake_string(compiler, "CMAKE_CXX_COMPILER_ID"),
        "compiler_version": cmake_string(compiler, "CMAKE_CXX_COMPILER_VERSION"),
        "cmake_version": cmake, "build_type": cache["CMAKE_BUILD_TYPE"],
        "cxx_standard": 20, "flags": tokens})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "build", "qt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--target", choices=("windows-x86_64", "linux-x86_64"), required=True)
    args = parser.parse_args()
    value = collect(args.repo, args.build, args.qt, args.target)
    with args.output.open("xb") as output:
        output.write(encoded(value))
    print(json.dumps(value, indent=2))


if __name__ == "__main__":
    main()
