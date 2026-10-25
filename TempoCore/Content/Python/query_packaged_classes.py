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
# The sim is given an endpoint of its own -- a uniquely named Unix domain socket where the platform
# has them, else a free ephemeral TCP port -- and the endpoint is probed before the launch. Both
# because the poll below accepts the first server that answers there, and on the default port that
# would be a developer's running editor, whose class list would then be written down as the build's.

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid

STARTUP_GRACE_SECONDS = 300
QUIT_GRACE_SECONDS = 30


def _launch(arguments, log):
    """Start the sim as the leader of a process group of its own.

    What a package calls its binary is not always the game. On Linux it is a launcher script that
    runs the game as a child, so signalling the process we started stops the script and orphans the
    game, which then keeps running. A group of its own is what lets _stop reach both.
    """
    if os.name == "nt":
        return subprocess.Popen(arguments, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    return subprocess.Popen(arguments, stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)


def _group_alive(process):
    try:
        os.killpg(process.pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def _signal_group(process, signal_number):
    try:
        os.killpg(process.pid, signal_number)
    except ProcessLookupError:
        pass


def _stop(process, quit_sim):
    """Stop the sim and everything it started.

    Asked first, as the test harness does: a sim that quits on its own takes its launcher with it.
    Signalled only if it does not, and then as a group, for the reason _launch gives. Waiting on
    the launcher alone would not do either, since it can exit while the game it started runs on.
    """
    if process.poll() is None:
        try:
            quit_sim()
        except Exception:  # never answered, or went away mid-call; the signals below still apply
            pass

    if os.name == "nt":
        try:
            process.wait(timeout=QUIT_GRACE_SECONDS)
        except subprocess.TimeoutExpired:
            # taskkill's /T is the Windows counterpart of signalling the group.
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            process.wait()
        return

    for signal_number in (None, signal.SIGTERM, signal.SIGKILL):
        if signal_number is not None:
            _signal_group(process, signal_number)
        deadline = time.monotonic() + QUIT_GRACE_SECONDS
        while _group_alive(process) and time.monotonic() < deadline:
            process.poll()  # reaps the launcher, which would otherwise linger as a zombie member
            time.sleep(0.5)
        if not _group_alive(process):
            break
    process.wait()


def _point_client_at(socket_name, port):
    """Launch arguments for the sim, and the call that points the client at the same place.

    The client has a separate entry point per transport -- set_socket for a Unix domain socket,
    set_server for a host and port -- rather than one call with a union of arguments.
    """
    import tempo_sim

    if socket_name:
        return ["-ServerSocket={}".format(socket_name)], lambda: tempo_sim.set_socket(socket_name)
    return ["-ServerPort={}".format(port)], lambda: tempo_sim.set_server(port=port)


def _free_port():
    """A TCP port nothing is listening on, from the kernel's ephemeral range.

    The port is free at the moment the kernel hands it out and the sim binds it moments later; the
    probe before the launch covers the interval. A fixed port could not give that guarantee, and the
    default one is the one a running editor holds.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def _describe(socket_name, port):
    return "socket {}".format(socket_name) if socket_name else "port {}".format(port)


def _endpoint_answers(socket_name, port):
    """Whether something already accepts connections where the sim is about to be told to listen.

    The server itself refuses to displace a live one (TempoServerEndpoint's liveness probe), so a
    taken endpoint makes the sim exit -- but not before the poll could have connected to whatever
    holds it and taken its answer for the build's. So the question is asked here, first.
    """
    if socket_name:
        # The same resolution the client applies when it is pointed at the name, less the scheme
        # gRPC wants in front of the path.
        from tempo_sim._tempo_context import socket_target
        address = socket_target(socket_name)[len("unix:"):]
        if not os.path.exists(address):
            return False
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    else:
        address = ("127.0.0.1", port)
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        probe.settimeout(1.0)
        probe.connect(address)
        return True
    except OSError:
        return False
    finally:
        probe.close()


def _surface(response):
    """What the build declares, as `filter_schema.py` needs to narrow the editor's document to it.

    The editor is the only thing that can report a property's metadata -- a BlueprintSetter, a
    category -- and the packaged build is the only authority on what exists. That is already why
    the class *list* is taken from here. Members are no different, and were being taken from the
    editor: measured on TempoSample, 101 classes have a property the editor has and the build does
    not, starting with AActor's seven WITH_EDITORONLY_DATA ones -- and AActor is the root of two
    thousand classes, so every actor in the generated package declared seven properties that fail
    at runtime. 54 classes likewise have an editor-only function (AActor::GetActorLabel among
    them), 48 have an editor-only component, and 96 report a different signature, usually a
    parameter whose name is cased differently in a cooked build. A CallFunction argument is matched
    by name, so that last one is not cosmetic.
    """
    def parameters(function):
        return [parameter.name for parameter in function.parameters]

    def members(descriptors):
        return {member.name: {"readable": member.readable,
                              "writable": member.writable,
                              "value_expressible": member.value_expressible}
                for member in descriptors}

    return {
        "classes": {
            cls.class_path: {
                "content_hash": cls.content_hash,
                "super": cls.super_path,
                "properties": members(cls.properties),
                "functions": {function.name: {"signature": function.signature,
                                              "parameters": parameters(function),
                                              "callable": function.callable,
                                              "positional": function.positional}
                              for function in cls.functions},
                "components": {component.name: component.class_path
                               for component in cls.components},
            }
            for cls in response.classes
        },
        "structs": {entry.name: {"content_hash": entry.content_hash,
                                 "value_expressible": entry.value_expressible,
                                 "members": members(entry.members)}
                    for entry in response.structs},
        "enums": {entry.name: {"content_hash": entry.content_hash,
                               "enumerators": list(entry.enumerators)}
                  for entry in response.enums},
    }


def query(binary, port, timeout, socket_name):
    import tempo_sim
    import tempo_sim.tempo_core as tempo_core
    import tempo_sim.tempo_world as tempo_world

    endpoint_args, point_client = _point_client_at(socket_name, port)
    arguments = [binary, "-nullrhi", "-unattended", "-nopause", "-nosound", "-nosplash"] + endpoint_args

    if _endpoint_answers(socket_name, port):
        raise RuntimeError(
            "something is already accepting connections on {}; whatever it is would answer in the "
            "sim's place, so the sim is not launched there".format(_describe(socket_name, port)))

    log = tempfile.NamedTemporaryFile(prefix="tempo-class-query-", suffix=".log", delete=False)
    print("  launching {} on {} (log: {})".format(os.path.basename(binary),
                                                  _describe(socket_name, port), log.name))
    process = _launch(arguments, log)

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
                # Every asset the build cooked, which is the one fact about an asset only the
                # build knows: an editor lists everything on disk, including what the cook dropped
                # because nothing referenced it. filter_schema.py decides what becomes the catalog.
                assets = tempo_world.get_assets(asset_type="/Script/CoreUObject.Object",
                                                include_subclasses=True)
                return {
                    "assets": [{"path": asset.path, "class": asset.class_path}
                               for asset in assets.assets],
                    "classes": [cls.class_path for cls in response.classes],
                    "surface": _surface(response),
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
        _stop(process, tempo_core.quit)


def main(argv):
    parser = argparse.ArgumentParser(
        description="Launch a packaged sim headless, ask it which classes it has, and write them.")
    parser.add_argument("binary", help="the packaged sim's binary, or its launcher script")
    parser.add_argument("out", help="where to write the JSON")
    parser.add_argument("--port", type=int, default=None,
                        help="TCP port to run the sim on when a Unix domain socket is not used; by "
                             "default a free ephemeral one, which nothing else can be answering on")
    parser.add_argument("--timeout", type=float, default=STARTUP_GRACE_SECONDS,
                        help="seconds to wait for the sim to answer (default: %(default)s)")
    args = parser.parse_args(argv[1:])

    binary, out_path, timeout = args.binary, args.out, args.timeout
    port = args.port if args.port is not None else _free_port()

    # A Unix domain socket first, where the platform has them, under a name no other process uses:
    # a package started here must not collide with an editor or another build, and must not be
    # answered for by one. The socket can still fail for reasons that have nothing to do with Tempo
    # -- the per-user socket directory may not be creatable in a restricted environment -- so the
    # port is the fallback rather than the end.
    attempts = []
    if os.name != "nt":
        attempts.append(("socket", "tempo-class-query-{}-{}.sock".format(os.getpid(), uuid.uuid4().hex[:8])))
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
