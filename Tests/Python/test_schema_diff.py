# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the release-to-release schema diff.

Pairs with the codegen tests: those check that drift is a type error for a client, these check that
it is reported before the release that causes it. Same list of cases, from the other end.

    pytest -m codegen Tests/Python
"""

import copy
import json
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DIFF = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "diff_schema.py")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

sys.path.insert(0, os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python"))
from diff_schema import compare, main  # noqa: E402


@pytest.fixture
def baseline():
    with open(FIXTURE) as f:
        return json.load(f)


def widget(schema):
    return next(c for c in schema["classes"] if c["path"] == "/Script/Engine.Widget")


def changes(baseline, mutate):
    """Apply a change to a copy of the baseline and return what the diff makes of it."""
    new = copy.deepcopy(baseline)
    mutate(new)
    breaking, additions = compare(baseline, new)
    return [change for _, change in breaking], [change for _, change in additions]


def exit_status(baseline, mutate, tmp_path):
    """What the tool exits with for the same change -- the status is what gates a pull request."""
    new = copy.deepcopy(baseline)
    mutate(new)
    old_path, new_path = tmp_path / "old.json", tmp_path / "new.json"
    old_path.write_text(json.dumps(baseline))
    new_path.write_text(json.dumps(new))
    return main(["diff_schema.py", str(old_path), str(new_path)])


def test_an_unchanged_schema_has_no_drift(baseline):
    breaking, additions = compare(baseline, copy.deepcopy(baseline))
    assert breaking == []
    assert additions == []


def test_a_removed_class_is_breaking(baseline):
    def mutate(new):
        new["classes"] = [c for c in new["classes"] if c["path"] != "/Script/Engine.Widget"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["class removed"]


def test_a_removed_property_is_breaking(baseline):
    def mutate(new):
        w = widget(new)
        w["properties"] = [p for p in w["properties"] if p["name"] != "bEnabled"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["property bEnabled removed"]


def test_a_retyped_property_is_breaking(baseline):
    def mutate(new):
        for p in widget(new)["properties"]:
            if p["name"] == "bEnabled":
                p["kind"] = p["type"] = "int"
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["property bEnabled: bool -> int"]


def test_a_property_becoming_read_only_is_breaking(baseline):
    def mutate(new):
        for p in widget(new)["properties"]:
            if p["name"] == "bEnabled":
                p["writable"] = False
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["property bEnabled became read-only"]


def test_a_changed_superclass_is_breaking(baseline):
    # It can remove every inherited member at once, which is as breaking as removing them singly.
    def mutate(new):
        widget(new)["super"] = ""
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["superclass: /Script/CoreUObject.Object -> (none)"]


def test_a_removed_function_is_breaking(baseline):
    def mutate(new):
        w = widget(new)
        w["functions"] = [f for f in w["functions"] if f["name"] != "Refresh"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["function Refresh removed"]


def test_a_changed_signature_is_breaking(baseline):
    def mutate(new):
        for f in widget(new)["functions"]:
            if f["name"] == "Resize":
                f["signature"] = "bool Resize(float NewSize)"
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["function Resize: bool Resize(int NewSize) -> bool Resize(float NewSize)"]


def test_a_function_becoming_uncallable_is_breaking(baseline):
    def mutate(new):
        for f in widget(new)["functions"]:
            if f["name"] == "Refresh":
                f["callable"] = False
                f["unsupported_reason"] = "now latent"
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["function Refresh is no longer callable: now latent"]


def test_a_function_that_is_no_longer_positional_is_breaking(baseline, tmp_path):
    """The generators withhold a function whose inputs cannot all travel as one value, so to a
    client it is gone -- gen_py_classes.py and gen_rust_classes.py both test `positional`."""
    def mutate(new):
        for f in widget(new)["functions"]:
            if f["name"] == "Refresh":
                f["positional"] = False
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["function Refresh is no longer offered: an input can no longer be passed "
                        "as one value"]
    assert exit_status(baseline, mutate, tmp_path) == 1


def test_a_removed_component_is_breaking(baseline, tmp_path):
    """A component is a typed accessor of its own, so losing one is losing a member."""
    def mutate(new):
        w = widget(new)
        w["components"] = [c for c in w["components"] if c["name"] != "Mesh"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["component Mesh removed"]
    assert exit_status(baseline, mutate, tmp_path) == 1


def test_a_component_whose_class_changed_is_breaking(baseline, tmp_path):
    """The accessor is typed as the component's class, so a different class is a different member
    under the same name: `widget.Mesh.Visible` type-checked against the old one and will not now."""
    def mutate(new):
        for c in widget(new)["components"]:
            if c["name"] == "Mesh":
                c["class"] = "/Script/Engine.Widget"
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["component Mesh: /Script/Engine.MeshThing -> /Script/Engine.Widget"]
    assert exit_status(baseline, mutate, tmp_path) == 1


def test_a_struct_member_becoming_read_only_is_breaking(baseline, tmp_path):
    """A writable member is a keyword argument of the struct's constructor; a read-only one is not,
    so `FWidgetSettings(Scale=2.0)` stops type-checking."""
    def mutate(new):
        for s in new["structs"]:
            if s["name"] == "FWidgetSettings":
                for m in s["members"]:
                    if m["name"] == "Scale":
                        m["writable"] = False
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["struct member Scale became read-only"]
    assert exit_status(baseline, mutate, tmp_path) == 1


def test_a_removed_enumerator_is_breaking(baseline):
    def mutate(new):
        for e in new["enums"]:
            if e["name"] == "EWidgetMode":
                e["enumerators"] = ["Simple"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["enumerator Advanced removed"]


def test_a_removed_struct_member_is_breaking(baseline):
    def mutate(new):
        for s in new["structs"]:
            if s["name"] == "FWidgetSettings":
                s["members"] = [m for m in s["members"] if m["name"] != "Scale"]
    breaking, _ = changes(baseline, mutate)
    assert breaking == ["struct member Scale removed"]


@pytest.mark.parametrize("case,mutate", [
    ("a new property", lambda new: widget(new)["properties"].append(
        {"name": "NewThing", "kind": "bool", "type": "bool",
         "readable": True, "writable": True, "value_expressible": True})),
    ("a new function", lambda new: widget(new)["functions"].append(
        {"name": "NewCall", "signature": "void NewCall()", "callable": True,
         "positional": True, "parameters": []})),
    ("a new enumerator", lambda new: new["enums"][0]["enumerators"].append("Expert")),
    ("a new component", lambda new: widget(new)["components"].append(
        {"name": "Extra", "class": "/Script/Engine.MeshThing"})),
    ("a new class", lambda new: new["classes"].append(
        {"name": "Other", "path": "/Script/Engine.Other", "super": "/Script/CoreUObject.Object",
         "package": "/Script/Engine", "abstract": False, "blueprint": False,
         "properties": [], "functions": []})),
])
def test_additions_break_nothing(baseline, case, mutate):
    breaking, additions = changes(baseline, mutate)
    assert breaking == [], "{} should not be breaking".format(case)
    assert len(additions) == 1


def test_documentation_changes_are_not_drift(baseline):
    # Deliberately not compared, for the same reason the content hash excludes it.
    def mutate(new):
        for p in widget(new)["properties"]:
            p["tooltip"] = "a freshly written explanation"
            p["category"] = "Reorganized"
    breaking, additions = changes(baseline, mutate)
    assert breaking == []
    assert additions == []


def test_the_tool_exits_nonzero_on_breaking_changes(baseline, tmp_path):
    """The exit status is what gates a pull request, so it is worth asserting directly."""
    old_path = tmp_path / "old.json"
    new_path = tmp_path / "new.json"
    old_path.write_text(json.dumps(baseline))

    unchanged = copy.deepcopy(baseline)
    new_path.write_text(json.dumps(unchanged))
    assert subprocess.run([sys.executable, DIFF, str(old_path), str(new_path)],
                          capture_output=True).returncode == 0

    broken = copy.deepcopy(baseline)
    w = widget(broken)
    w["properties"] = [p for p in w["properties"] if p["name"] != "bEnabled"]
    new_path.write_text(json.dumps(broken))
    result = subprocess.run([sys.executable, DIFF, str(old_path), str(new_path)],
                            capture_output=True, text=True)
    assert result.returncode == 1
    assert "bEnabled" in result.stdout
    assert "breaking change" in result.stdout



def diff(baseline, mutate):
    """As `changes`, but keeping who each change is about."""
    new = copy.deepcopy(baseline)
    mutate(new)
    return compare(baseline, new)


def test_a_removed_asset_is_breaking(baseline):
    def mutate(new):
        new["assets"] = [a for a in new["assets"] if a["path"] != "/Game/Widgets/W_Main.W_Main"]
    breaking, _ = diff(baseline, mutate)
    assert ("/Game/Widgets/W_Main.W_Main", "asset removed") in breaking


def test_an_asset_whose_class_changed_is_breaking(baseline):
    def mutate(new):
        for asset in new["assets"]:
            if asset["path"] == "/Game/Widgets/W_Main.W_Main":
                asset["class"] = "/Script/Engine.Widget"
    breaking, _ = diff(baseline, mutate)
    assert any(owner == "/Game/Widgets/W_Main.W_Main" and "asset class" in change
               for owner, change in breaking)


def test_an_added_asset_is_not_breaking(baseline):
    def mutate(new):
        new["assets"].append({"path": "/Game/New.New", "class": "/Script/Engine.MeshThing"})
    breaking, additions = diff(baseline, mutate)
    assert breaking == []
    assert ("/Game/New.New", "asset added") in additions


def test_no_catalog_on_one_side_compares_no_assets(baseline):
    """An editor schema has none; diffing it against a packaged one must not remove every asset."""
    def mutate(new):
        new.pop("assets")
    breaking, _ = diff(baseline, mutate)
    assert not any(change == "asset removed" for _, change in breaking)


def test_a_struct_that_stops_travelling_whole_is_breaking(baseline):
    def mutate(new):
        for struct in new["structs"]:
            if struct["name"] == "FWidgetSettings":
                struct["value_expressible"] = False
    breaking, _ = diff(baseline, mutate)
    assert ("FWidgetSettings", "struct can no longer be passed as one value") in breaking
