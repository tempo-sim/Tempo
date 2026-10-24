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
from diff_schema import compare  # noqa: E402


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
