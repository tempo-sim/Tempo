"""Compile the typed C++ classes into libtempo_reflected and install them into the C++ client.

    build_cpp_classes.py <generated-dir> <cpp-api-dir> <grpc-dir>

`<generated-dir>` is what gen_cpp_classes.py wrote; `<cpp-api-dir>` the C++ client
(TempoCore/Content/Cpp/API), whose headers the classes compile against and into which the headers
and the archive are installed -- `include/tempo/reflected/` and `lib/<Platform>/`; `<grpc-dir>` the
vendored gRPC (TempoCore/Source/ThirdParty/gRPC), for its headers.

A second archive rather than more of libtempo, because the two are built at different times:
libtempo at prebuild, from the protos, and this at package time, from the packaged build's own
reflection (TYPED_WORLD_API_PLAN.md §23.4).
"""

import os
import platform
import shutil
import subprocess
import sys

from gen_naming import build_jobs, cpp_compiler_args, host_platform_name

ARCHIVE_STEM = "tempo_reflected"


def build(generated, api_dir, grpc_dir):
    build_dir = os.path.join(generated, "cmake")
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    configure = ["cmake", "-S", generated, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release",
                 "-DTEMPO_API_DIR=" + api_dir, "-DTEMPO_GRPC_DIR=" + grpc_dir] + cpp_compiler_args()
    if platform.system() == "Windows" and shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", build_dir, "--config", "Release", "--parallel", str(build_jobs())],
                   check=True)

    archives = [os.path.join(root, name) for root, _, names in os.walk(os.path.join(build_dir, "lib"))
                for name in names if name.endswith((".a", ".lib"))]
    if not archives:
        raise RuntimeError("no archive under {}; the build produced nothing".format(build_dir))

    installed_headers = os.path.join(api_dir, "include", "tempo", "reflected")
    if os.path.exists(installed_headers):
        shutil.rmtree(installed_headers)
    shutil.copytree(os.path.join(generated, "include", "tempo", "reflected"), installed_headers)

    lib_dir = os.path.join(api_dir, "lib", host_platform_name())
    os.makedirs(lib_dir, exist_ok=True)
    for name in os.listdir(lib_dir):
        if ARCHIVE_STEM in name:
            os.remove(os.path.join(lib_dir, name))
    for archive in archives:
        shutil.copy2(archive, lib_dir)
    return lib_dir


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    generated, api_dir, grpc_dir = (os.path.abspath(path) for path in argv[1:])
    if shutil.which("cmake") is None:
        print("cmake is not on PATH; it is needed to build the typed C++ classes", file=sys.stderr)
        return 1
    lib_dir = build(generated, api_dir, grpc_dir)
    print("Typed C++ classes installed into {} (archive in {})".format(api_dir, lib_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
