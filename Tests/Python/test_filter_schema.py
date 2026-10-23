# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for narrowing the editor's schema to what the packaged build actually ships.

The editor is the only thing that can report a property's metadata and the build is the only
authority on what exists, so the two have to be combined. Narrowing the class list was only half of
that, and the half that was missing is the one these are mostly about: a class both have can still
declare members only the editor does.

    Scripts/TestPythonAPI.sh codegen
    pytest -m codegen Tests/Python/test_filter_schema.py
"""

import copy
import json
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PYTHON_DIR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

sys.path.insert(0, PYTHON_DIR)
import filter_schema  # noqa: E402

pytestmark = pytest.mark.codegen


def member(readable=True, writable=True, value_expressible=True):
    return {"readable": readable, "writable": writable,
            "value_expressible": value_expressible}


@pytest.fixture
def schema():
    with open(FIXTURE) as f:
        return json.load(f)


@pytest.fixture
def surface(schema):
    """A build that declares exactly what the editor's document does, as the baseline.

    Each case then takes something away from it, which is how the real difference presents: the
    editor has more.
    """
    return {
        "classes": {
            cls["path"]: {
                "content_hash": "BUILD-" + cls["name"].upper(),
                "super": cls.get("super", ""),
                "properties": {m["name"]: member(m["readable"], m["writable"],
                                                 m["value_expressible"])
                               for m in cls["properties"]},
                "functions": {f["name"]: {"signature": f["signature"],
                                          "parameters": [p["name"]
                                                         for p in f.get("parameters", [])],
                                          "callable": f.get("callable", True)}
                              for f in cls["functions"]},
                "components": {c["name"]: c["class"] for c in cls["components"]},
            }
            for cls in schema["classes"]
        },
        "structs": {
            struct["name"]: {"content_hash": "BUILD-" + struct["name"].upper(),
                             "members": {m["name"]: member() for m in struct["members"]}}
            for struct in schema["structs"]
        },
        "enums": {
            enum["name"]: {"content_hash": "BUILD-" + enum["name"].upper(),
                           "enumerators": list(enum["enumerators"])}
            for enum in schema["enums"]
        },
    }


def widget(schema):
    return next(cls for cls in schema["classes"] if cls["path"] == "/Script/Engine.Widget")


def names(entries):
    return [entry["name"] for entry in entries]


def test_nothing_changes_when_the_build_agrees(schema, surface):
    before = copy.deepcopy(schema)
    stats = filter_schema.narrow_to_surface(schema, surface)
    assert stats["members_dropped"] == 0
    assert stats["functions_dropped"] == 0
    assert stats["components_dropped"] == 0
    assert stats["signatures_corrected"] == 0
    assert names(widget(schema)["properties"]) == names(widget(before)["properties"])


def test_a_property_the_build_does_not_have_is_dropped(schema, surface):
    """AActor's seven WITH_EDITORONLY_DATA properties, at fixture scale.

    They were reaching the stubs of every one of the two thousand classes that derive from AActor,
    and each one type-checked and then failed at runtime -- the one outcome the typed API exists to
    prevent.
    """
    del surface["classes"]["/Script/Engine.Widget"]["properties"]["bEnabled"]
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["members_dropped"] == 1
    assert "bEnabled" not in names(widget(schema)["properties"])
    # Everything else is left alone.
    assert "Tags" in names(widget(schema)["properties"])


def test_a_function_the_build_does_not_have_is_dropped(schema, surface):
    del surface["classes"]["/Script/Engine.Widget"]["functions"]["Refresh"]
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["functions_dropped"] == 1
    assert "Refresh" not in names(widget(schema)["functions"])


def test_a_component_the_build_does_not_have_is_dropped(schema, surface):
    """An editor-only billboard, which is what 48 real classes differ by."""
    del surface["classes"]["/Script/Engine.Widget"]["components"]["Mesh"]
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["components_dropped"] == 1
    assert "Mesh" not in names(widget(schema)["components"])


def test_a_component_whose_class_changed_is_dropped(schema, surface):
    """A component accessor is typed as its class, so a different class is a different member."""
    surface["classes"]["/Script/Engine.Widget"]["components"]["Mesh"] = "/Script/Engine.Other"
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["components_dropped"] == 1
    assert "Mesh" not in names(widget(schema)["components"])


def test_a_signature_and_its_parameter_names_come_from_the_build(schema, surface):
    """A cooked build cases some parameter names differently, and this is not cosmetic.

    A CallFunction argument is matched against the function's own parameters by name, so an
    editor-sourced name is a FAILED_PRECONDITION at the first call -- which is exactly how the
    BlueprintSetter write path was broken before.
    """
    reported = surface["classes"]["/Script/Engine.Widget"]["functions"]["Resize"]
    reported["signature"] = "bool Resize(int newsize)"
    reported["parameters"] = ["newsize"] + reported["parameters"][1:]

    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["signatures_corrected"] == 1
    resize = next(f for f in widget(schema)["functions"] if f["name"] == "Resize")
    assert resize["signature"] == "bool Resize(int newsize)"
    assert resize["parameters"][0]["name"] == "newsize"


def test_parameter_names_are_left_alone_when_the_count_disagrees(schema, surface):
    """Then the two documents disagree about the function itself, and the names are no use.

    The lists are positional in both, so zipping a shorter one onto a longer one would rename the
    wrong parameters -- which for a setter means sending an argument the server will reject.
    """
    reported = surface["classes"]["/Script/Engine.Widget"]["functions"]["Resize"]
    reported["signature"] = "bool Resize()"
    reported["parameters"] = []

    filter_schema.narrow_to_surface(schema, surface)

    resize = next(f for f in widget(schema)["functions"] if f["name"] == "Resize")
    assert resize["signature"] == "bool Resize()"
    assert names(resize["parameters"]) == ["NewSize", "ReturnValue"]


def test_flags_come_from_the_build(schema, surface):
    surface["classes"]["/Script/Engine.Widget"]["properties"]["bEnabled"]["writable"] = False
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["flags_corrected"] == 1
    enabled = next(m for m in widget(schema)["properties"] if m["name"] == "bEnabled")
    assert enabled["writable"] is False


def test_a_member_only_the_build_has_is_counted_and_left_out(schema, surface):
    """Carrying one over would mean no metadata for it, and so no BlueprintSetter.

    Writing such a property directly skips an accessor that may be the only thing clamping the
    value or marking render state dirty, so the property gets written *wrongly* rather than not at
    all. Offering less is the documented failure mode; offering something unsafe is not.
    """
    surface["classes"]["/Script/Engine.Widget"]["properties"]["benabled"] = member()
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["members_only_in_build"] == 1
    assert "benabled" not in names(widget(schema)["properties"])


def test_a_struct_is_narrowed_too(schema, surface):
    """72 real structs differ between the editor and the build, as 270 classes do."""
    settings = next(s for s in schema["structs"] if s["name"] == "FWidgetSettings")
    declared = len(settings["members"])
    assert declared, "the fixture struct should have members to drop"
    surface["structs"]["FWidgetSettings"]["members"] = {}

    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["members_dropped"] == declared
    assert settings["members"] == []


def test_every_hash_comes_from_the_build(schema, surface):
    """The editor's numbers would mismatch for 270 real classes, AActor among them.

    `check_schema` compares what a package recorded against what the sim reports, so a package
    stamped with the editor's hashes would fail for every project. Taking the build's number is
    only honest once the package is a subset of what the build has, which is what the dropping
    above achieves.
    """
    filter_schema.narrow_to_surface(schema, surface)

    for cls in schema["classes"]:
        assert cls["content_hash"] == "BUILD-" + cls["name"].upper()
    for entry in schema["structs"] + schema["enums"]:
        assert entry["content_hash"] == "BUILD-" + entry["name"].upper()


def test_the_package_declares_nothing_the_build_lacks(schema, surface):
    """The invariant the whole exercise buys, stated over the whole document.

    Asserted rather than left to the cases above, because the next schema will differ from the
    fixture and this is the property that has to keep holding.
    """
    del surface["classes"]["/Script/Engine.Widget"]["properties"]["bEnabled"]
    del surface["classes"]["/Script/Engine.Widget"]["functions"]["Refresh"]
    del surface["classes"]["/Script/Engine.Widget"]["components"]["Mesh"]
    filter_schema.narrow_to_surface(schema, surface)

    for cls in schema["classes"]:
        declared = surface["classes"][cls["path"]]
        assert set(names(cls["properties"])) <= set(declared["properties"])
        assert set(names(cls["functions"])) <= set(declared["functions"])
        for component in cls["components"]:
            assert declared["components"].get(component["name"]) == component["class"]


def test_a_class_the_build_does_not_report_is_left_alone(schema, surface):
    """Narrowing runs after the class filter, so this is the 26 classes the build has and the
    editor's walk does not -- the reverse case. Nothing to narrow against means nothing to do.
    """
    del surface["classes"]["/Script/Engine.Widget"]
    before = copy.deepcopy(widget(schema))
    filter_schema.narrow_to_surface(schema, surface)

    assert widget(schema) == before


def test_a_report_without_a_surface_narrows_only_the_class_list(tmp_path):
    """Backwards compatible, and loud about it: an older report cannot narrow members."""
    reported = {"classes": ["/Script/Engine.Widget", "/Script/CoreUObject.Object"],
                "header": {"schema_version": "2", "engine_version": "5.8",
                           "content_hash": "BUILD"}}
    report = tmp_path / "reported.json"
    report.write_text(json.dumps(reported))
    out = tmp_path / "filtered.json"

    result = subprocess.run(
        [sys.executable, os.path.join(PYTHON_DIR, "filter_schema.py"), FIXTURE,
         str(report), str(out)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert "no declared surface" in result.stderr

    filtered = json.loads(out.read_text())
    assert sorted(cls["path"] for cls in filtered["classes"]) == sorted(reported["classes"])
