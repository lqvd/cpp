from os import makedirs
from os.path import dirname, exists, join, abspath, isabs
from glob import glob
from invoke import task

from faasmtools.env import PROJ_ROOT, THIRD_PARTY_DIR, LLVM_NATIVE_VERSION

CPP_ROOT = PROJ_ROOT
BUILD_DIR = join(CPP_ROOT, "build")
PLUGIN_BUILD_DIR = join(BUILD_DIR, "protogen-faabric")
PLUGIN_BIN = join(PLUGIN_BUILD_DIR, "protoc-gen-granny")

PROTOGEN_SRC = join(THIRD_PARTY_DIR, "protogen-faabric")

def _cmake_build_cmd(build_dir, src_dir):
    llvm_major = LLVM_NATIVE_VERSION.split(".")[0]
    return (
        f"cmake -S {src_dir} -B {build_dir} "
        f"-DCMAKE_C_COMPILER=/usr/bin/clang-{llvm_major} "
        f"-DCMAKE_CXX_COMPILER=/usr/bin/clang++-{llvm_major} "
        f"-DCMAKE_AR=/usr/bin/llvm-ar-{llvm_major} "
        f"-DCMAKE_NM=/usr/bin/llvm-nm-{llvm_major} "
        f"-DCMAKE_RANLIB=/usr/bin/llvm-ranlib-{llvm_major} && "
        f"cmake --build {build_dir}"
    )

@task
def build_plugin(ctx):
    """
    Build the host protoc plugin into clients/cpp/build.
    """
    if not exists(BUILD_DIR):
        makedirs(BUILD_DIR)

    cmd = _cmake_build_cmd(PLUGIN_BUILD_DIR, PROTOGEN_SRC)
    print(cmd)
    ctx.run(cmd)

@task
def generate(ctx, proto_dir="clients/cpp/func/rpc"):
    """
    Generate stubs next to each .proto file.
    """
    abs_proto_dir = abspath(join(PROJ_ROOT, proto_dir))
    protos = glob(join(abs_proto_dir, "*.proto"))
    if not protos:
        raise RuntimeError(f"No .proto files found in {abs_proto_dir}")

    if not exists(PLUGIN_BIN):
        raise RuntimeError(
            f"Plugin not found at {PLUGIN_BIN}. "
            "Run `inv pb.build-plugin` first."
        )

    for proto in protos:
        out_dir = dirname(proto)
        cmd = (
            f"protoc "
            f"--plugin=protoc-gen-faabric={PLUGIN_BIN} "
            f"--faabric_out={out_dir} "
            f"--cpp_out={out_dir} "
            f"-I {abs_proto_dir} "
            f"{proto}"
        )
        print(cmd)
        ctx.run(cmd)