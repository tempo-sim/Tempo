# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Generates the source files TempoAgentsEditor derives from the engine's ZoneGraph plugin.

Tempo changes how ZoneGraph lays lanes through intersections. The code that does it is private to
the engine's ZoneGraph module, so instead of modifying the engine, Tempo compiles its own edited
copy. That copy is never checked in. It is generated here, at build time, from the engine source
already on the machine and a list of edits stored in the repo.

Edits are line-range operations on one exact version of an engine file, identified by the SHA-256
of its contents: "delete these lines", "add these lines after that one". They hold the lines Tempo
adds and none of the engine's. A given edit list only ever applies to the file it was made from, so
an engine version whose file differs is unsupported until an edit list is added for it.

Usage:
    gen_engine_derived.py <EngineDir> <PluginDir>            Generate (what the build runs).
    gen_engine_derived.py <EngineDir> <PluginDir> --extract [<label>]
                                                             Store the current generated files'
                                                             differences from the engine as edits,
                                                             recorded as supporting the engine's
                                                             version, or <label> if given.

Earlier versions of Tempo modified the engine's ZoneGraph plugin in place. The files as those mods
left them are supported like any other version, so such an engine needs no repair.

Set TEMPO_ENGINE_SOURCE_OVERRIDE to a directory laid out like <EngineDir> to read the engine's files
from somewhere else.
"""

import difflib
import hashlib
import json
import os
import sys

MANIFEST_DIR = os.path.join("Source", "TempoAgentsEditor", "EngineDerived")
MANIFEST_NAME = "Manifest.json"
EDITS_HEADER = "# tempo-engine-derived-edits 1"


def fail(message):
    sys.stderr.write("\ngen_engine_derived.py: error: " + message + "\n\n")
    sys.exit(1)


def read_lines(path):
    """The file's lines without their terminators. Windows engine installs have CRLF line endings."""
    with open(path, "r", encoding="utf-8", newline="") as file:
        text = file.read()
    text = text.replace("\r\n", "\n")
    if text.startswith("\ufeff"):
        text = text[1:]
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def sha256_of(lines):
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def engine_version(engine_dir):
    try:
        with open(os.path.join(engine_dir, "Build", "Build.version"), "r", encoding="utf-8") as file:
            version = json.load(file)
        return "{}.{}.{}".format(version["MajorVersion"], version["MinorVersion"], version["PatchVersion"])
    except (OSError, ValueError, KeyError):
        return "unknown"


def make_edits(source_lines, output_lines):
    """The edits that turn source_lines into output_lines, as (command, line, count, added lines)."""
    edits = []
    matcher = difflib.SequenceMatcher(None, source_lines, output_lines, autojunk=False)
    for tag, source_begin, source_end, output_begin, output_end in matcher.get_opcodes():
        if tag in ("delete", "replace"):
            edits.append(("d", source_begin + 1, source_end - source_begin, None))
        if tag in ("insert", "replace"):
            edits.append(("a", source_end, output_end - output_begin, output_lines[output_begin:output_end]))
    return edits


def format_edits(source, source_hash, output_hash, edits):
    """
    d<line> <count>   delete <count> lines of the source, starting at <line>
    a<line> <count>   add the <count> lines that follow after <line> of the source
    Line numbers are the source's, counted from 1, and commands are in source order.
    """
    out = [EDITS_HEADER, "# source: " + source, "# source-sha256: " + source_hash, "# output-sha256: " + output_hash]
    for command, line, count, added in edits:
        out.append("{}{} {}".format(command, line, count))
        if added is not None:
            out.extend(added)
    return "\n".join(out) + "\n"


def parse_edits(path):
    lines = read_lines(path)
    if not lines or lines[0] != EDITS_HEADER:
        fail("{} is not an edits file this script understands.".format(path))
    header = {}
    index = 1
    while index < len(lines) and lines[index].startswith("# "):
        key, _, value = lines[index][2:].partition(": ")
        header[key] = value
        index += 1
    edits = []
    while index < len(lines):
        command = lines[index][:1]
        try:
            line, count = (int(field) for field in lines[index][1:].split(" "))
        except ValueError:
            fail("{}:{}: expected a command, found '{}'.".format(path, index + 1, lines[index]))
        index += 1
        if command == "d":
            edits.append(("d", line, count, None))
        elif command == "a":
            edits.append(("a", line, count, lines[index:index + count]))
            index += count
        else:
            fail("{}:{}: unknown command '{}'.".format(path, index, command))
    return header, edits


def apply_edits(source_lines, edits):
    output = []
    cursor = 0  # Index of the next source line not yet copied or deleted.
    for command, line, count, added in edits:
        if command == "d":
            output.extend(source_lines[cursor:line - 1])
            cursor = line - 1 + count
        else:
            if line > cursor:
                output.extend(source_lines[cursor:line])
                cursor = line
            output.extend(added)
    output.extend(source_lines[cursor:])
    return output


def load_manifest(plugin_dir):
    path = os.path.join(plugin_dir, MANIFEST_DIR, MANIFEST_NAME)
    with open(path, "r", encoding="utf-8") as file:
        return json.load(file)


def save_manifest(plugin_dir, manifest):
    path = os.path.join(plugin_dir, MANIFEST_DIR, MANIFEST_NAME)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(manifest, file, indent=2)
        file.write("\n")


def write_if_changed(path, text):
    try:
        with open(path, "r", encoding="utf-8", newline="") as file:
            if file.read() == text:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(text)
    return True


def source_path(engine_dir, source):
    root = os.environ.get("TEMPO_ENGINE_SOURCE_OVERRIDE") or engine_dir
    return os.path.join(root, *source.split("/"))


def generate(engine_dir, plugin_dir):
    manifest = load_manifest(plugin_dir)
    output_dir = os.path.join(plugin_dir, *manifest["OutputDir"].split("/"))
    for entry in manifest["Sources"]:
        path = source_path(engine_dir, entry["Source"])
        if not os.path.isfile(path):
            fail("The engine source file {} was not found. Tempo generates part of TempoAgentsEditor from it, "
                 "so it needs an engine installed with its plugin source.".format(path))
        source_lines = read_lines(path)
        source_hash = sha256_of(source_lines)
        edits_entry = next((known for known in entry["Edits"] if known["SourceSHA256"] == source_hash), None)
        if edits_entry is None:
            supported = sorted({version for known in entry["Edits"] for version in known["EngineVersions"]})
            fail("Tempo does not support this engine's copy of\n  {}\n"
                 "(Unreal Engine {}, SHA-256 {}).\n"
                 "Tempo supports the file as released in Unreal Engine: {}.\n"
                 "If the file has been modified, verify the engine installation to restore it."
                 .format(path, engine_version(engine_dir), source_hash, ", ".join(supported)))
        header, edits = parse_edits(os.path.join(plugin_dir, MANIFEST_DIR, edits_entry["File"]))
        output_lines = apply_edits(source_lines, edits)
        if sha256_of(output_lines) != header.get("output-sha256"):
            fail("Applying {} did not produce the file it was made to produce.".format(edits_entry["File"]))
        if write_if_changed(os.path.join(output_dir, entry["Output"]), "\n".join(output_lines) + "\n"):
            print("Generated {} from the engine's {}".format(entry["Output"], os.path.basename(entry["Source"])))


def extract(engine_dir, plugin_dir, label):
    manifest = load_manifest(plugin_dir)
    output_dir = os.path.join(plugin_dir, *manifest["OutputDir"].split("/"))
    version = label or engine_version(engine_dir)
    for entry in manifest["Sources"]:
        source_lines = read_lines(source_path(engine_dir, entry["Source"]))
        output_lines = read_lines(os.path.join(output_dir, entry["Output"]))
        source_hash = sha256_of(source_lines)
        edits = make_edits(source_lines, output_lines)
        if apply_edits(source_lines, edits) != output_lines:
            fail("Could not express {} as edits to {}.".format(entry["Output"], entry["Source"]))
        edits_file = "{}.{}.edits".format(os.path.basename(entry["Source"]), source_hash[:12])
        text = format_edits(entry["Source"], source_hash, sha256_of(output_lines), edits)
        write_if_changed(os.path.join(plugin_dir, MANIFEST_DIR, edits_file), text)
        edits_entry = next((known for known in entry["Edits"] if known["SourceSHA256"] == source_hash), None)
        if edits_entry is None:
            edits_entry = {"SourceSHA256": source_hash, "File": edits_file, "EngineVersions": []}
            entry["Edits"].append(edits_entry)
        edits_entry["File"] = edits_file
        if version != "unknown" and version not in edits_entry["EngineVersions"]:
            edits_entry["EngineVersions"] = sorted(edits_entry["EngineVersions"] + [version])
        added = sum(count for command, _, count, _ in edits if command == "a")
        deleted = sum(count for command, _, count, _ in edits if command == "d")
        print("{}: {} lines added, {} deleted -> {}".format(entry["Output"], added, deleted, edits_file))
    save_manifest(plugin_dir, manifest)


def main(argv):
    if len(argv) < 3 or (len(argv) > 3 and argv[3] != "--extract") or len(argv) > 5:
        fail("usage: gen_engine_derived.py <EngineDir> <PluginDir> [--extract [<label>]]")
    engine_dir, plugin_dir = argv[1], argv[2]
    if len(argv) > 3:
        extract(engine_dir, plugin_dir, argv[4] if len(argv) == 5 else None)
    else:
        generate(engine_dir, plugin_dir)


if __name__ == "__main__":
    main(sys.argv)
