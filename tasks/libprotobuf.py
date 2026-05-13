from faasmtools.build import CMAKE_TOOLCHAIN_FILE, get_faasm_build_env_dict
from faasmtools.env import PROJ_ROOT, USABLE_CPUS
from invoke import task

from os import environ, makedirs
from os.path import exists, join
from shutil import rmtree
from subprocess import run


PROTOBUF_SRC_DIR = join(PROJ_ROOT, "third-party", "protobuf")
PROTOBUF_BUILD_DIR = join(PROTOBUF_SRC_DIR, "build-wasm32-wasi-threads")

@task(default=True)
def build(ctx, clean=False):
    """
    Build and install protobuf-lite for Faasm wasm32-wasi-threads.
    """
    if not exists(join(PROTOBUF_SRC_DIR, "CMakeLists.txt")):
        raise RuntimeError(
            "Could not find CMakeLists.txt in {}".format(PROTOBUF_SRC_DIR)
        )

    build_env = environ.copy()
    build_env.update(get_faasm_build_env_dict(is_threads=True))

    if clean and exists(PROTOBUF_BUILD_DIR):
        rmtree(PROTOBUF_BUILD_DIR)

    makedirs(PROTOBUF_BUILD_DIR, exist_ok=True)

    configure_cmd = [
        "cmake",
        "-GNinja",
        "-S",
        PROTOBUF_SRC_DIR,
        "-B",
        PROTOBUF_BUILD_DIR,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TOOLCHAIN_FILE={}".format(CMAKE_TOOLCHAIN_FILE),
        "-DCMAKE_INSTALL_PREFIX={}".format(build_env["FAASM_WASM_SYSROOT"]),
        "-Dprotobuf_BUILD_TESTS=OFF",
        "-Dprotobuf_BUILD_PROTOC_BINARIES=OFF",
        "-Dprotobuf_BUILD_LIBPROTOC=OFF",
        "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
        "-Dprotobuf_WITH_ZLIB=OFF",
        "-Dprotobuf_DISABLE_RTTI=ON",
    ]

    run(configure_cmd, check=True, cwd=PROJ_ROOT, env=build_env)

    run(
        ["ninja", "-j", str(USABLE_CPUS)],
        check=True,
        cwd=PROTOBUF_BUILD_DIR,
        env=build_env,
    )

    run(
        ["ninja", "install"],
        check=True,
        cwd=PROTOBUF_BUILD_DIR,
        env=build_env,
    )