# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Launches a packaged sim headless, asks it which classes it has, and writes their paths.
#
# Used by GenTypedAPI.sh to narrow the editor-generated schema to what actually ships. The editor is
# the only thing that can report property metadata, and the packaged build is the only authority on
# which classes exist, so the two have to be combined -- and this is the half that asks the build.
#
#   python3 query_packaged_classes.py <packaged-binary> <out.json> [--port N] [--timeout S]
#
# Prefers a Unix domain socket where one is supported, so that a package running on a developer's
# machine cannot collide with an editor or another build already holding the default TCP port.

import json
import os
import subprocess
import sys
import tempfile
import time

STARTUP_GRACE_SECONDS = 300


def _point_client_at(socket_name, port):
    """Launch arguments for the sim, and the call that points the client at the same place.

    The client has a separate entry point per transport -- set_socket for a Unix domain socket,
    set_server for a host and port -- rather than one call with a union of arguments.
    """
    import tempo_sim

    if socket_name:
        return ["-ServerSocket={}".format(socket_name)], lambda: tempo_sim.set_socket(socket_name)
    return ["-ServerPort={}".format(port)], lambda: tempo_sim.set_server(port=port)


def query(binary, port, timeout, socket_name):
    import tempo_sim
    import tempo_sim.tempo_world as tempo_world

    endpoint_args, point_client = _point_client_at(socket_name, port)
    arguments = [binary, "-nullrhi", "-unattended", "-nopause", "-nosound", "-nosplash"] + endpoint_args

    log = tempfile.NamedTemporaryFile(prefix="tempo-class-query-", suffix=".log", delete=False)
    print("  launching {} (log: {})".format(os.path.basename(binary), log.name))
    process = subprocess.Popen(arguments, stdout=log, stderr=subprocess.STDOUT)

    try:
        point_client()
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("the sim exited with {} before answering; see {}".format(
                    process.returncode, log.name))
            try:
                # These three options have to match what world.check_schema() asks for, exactly.
                # The hash covers function signatures, so asking without functions yields a
                # different number for the same build -- and a hash that depends on how it was
                # requested cannot answer "do these stubs match this sim".
                #
                # Blueprint loading is also the expensive half of a walk, and the stubs need those
                # classes, so it is not optional either.
                response = tempo_world.describe_classes(include_blueprint_classes=True,
                                                        blueprint_accessible_only=True,
                                                        include_functions=True)
                return {
                    "classes": [cls.class_path for cls in response.classes],
                    # The build's own identity, so the generated client can be stamped with the
                    # hash of the sim it was made for rather than of the editor that walked it.
                    "header": {
                        "schema_version": response.header.schema_version,
                        "engine_version": response.header.engine_version,
                        "content_hash": response.header.content_hash,
                    },
                }
            except Exception as error:  # the server is not up yet, or is still loading
                last_error = error
                time.sleep(2.0)
        raise RuntimeError("the sim did not answer within {}s: {}".format(timeout, last_error))
    finally:
        process.terminate()
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()


def main(argv):
    if len(argv) < 3:
        print("usage: query_packaged_classes.py <packaged-binary> <out.json> [--port N] [--timeout S]",
              file=sys.stderr)
        return 2

    binary, out_path = argv[1], argv[2]
    port = int(argv[argv.index("--port") + 1]) if "--port" in argv else 10001
    timeout = float(argv[argv.index("--timeout") + 1]) if "--timeout" in argv else STARTUP_GRACE_SECONDS

    # A Unix domain socket first, where the platform has them: a package started here must not
    # collide with an editor or another build already holding the default TCP port. It can still
    # fail for reasons that have nothing to do with Tempo -- the per-user socket directory may not
    # be creatable in a restricted environment -- so a port is the fallback rather than the end.
    attempts = []
    if os.name != "nt":
        attempts.append(("socket", "tempo-class-query-{}.sock".format(os.getpid())))
    attempts.append(("port", ""))

    reported, failures = None, []
    for kind, socket_name in attempts:
        try:
            reported = query(binary, port, timeout, socket_name)
            break
        except Exception as error:
            failures.append("{}: {}".format(kind, error))

    if reported is None:
        for failure in failures:
            print("  failed to query the packaged build via {}".format(failure), file=sys.stderr)
        return 1

    if not reported["classes"]:
        print("  the build reported no classes at all, which cannot be right", file=sys.stderr)
        return 1

    reported["classes"] = sorted(reported["classes"])
    with open(out_path, "w") as f:
        json.dump(reported, f, indent=1)
        f.write("\n")
    print("  the packaged build reports {} classes".format(len(reported["classes"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
