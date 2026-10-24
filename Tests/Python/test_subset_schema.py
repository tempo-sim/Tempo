# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the published class subset.

`subset_schema.py` narrows a schema to the modules that ship in every build, so the classes can go
out in the published `tempo-sim` wheel and crate rather than only beside a project's own package.
See TYPED_WORLD_API_PLAN.md section 16.

Two kinds of case here. The structural ones assert the subset's invariant directly -- every kept
class's superclass is in the document -- because that is what both generators depend on and what a
module-chosen subset is the first thing to break. The rest go through Pyright, as the rest of this
group does: what matters is not that the stubs changed but whether a checker still accepts what it
must and rejects what it must.

    Scripts/TestPythonAPI.sh codegen
    pytest -m codegen Tests/Python/test_subset_schema.py
"""

import json
import os
import subprocess
import sys

import pytest

from test_codegen import DIST, GENERATOR, check

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PYTHON_DIR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

sys.path.insert(0, PYTHON_DIR)
import subset_schema  # noqa: E402

pytestmark = pytest.mark.codegen

# The fixture's published packages. `/Script/SomePlugin` is deliberately absent: a project need not
# enable a plugin, which is the whole criterion.
FIXTURE_PACKAGES = ("/Script/CoreUObject", "/Script/Engine")


@pytest.fixture(scope="module")
def subset():
    """The fixture schema, narrowed to its published packages."""
    with open(FIXTURE) as f:
        schema = json.load(f)
    narrowed, rerooted = subset_schema.subset(schema, FIXTURE_PACKAGES)
    return narrowed, {path: (new_super, skipped) for path, new_super, skipped in rerooted}


# -- the module set ------------------------------------------------------------------------------

def test_the_published_set_is_the_modules_that_always_ship():
    packages = subset_schema.published_packages(PLUGIN_ROOT)

    for engine in subset_schema.ENGINE_PACKAGES:
        assert engine in packages
    # Tempo's own, derived from the descriptors rather than listed.
    for module in ("TempoCore", "TempoWorld", "TempoSensors", "TempoMovement"):
        assert "/Script/" + module in packages, "a Tempo runtime module should be published"

    # An Editor module is not in a packaged build at all.
    assert "/Script/TempoCoreEditor" not in packages
    assert "/Script/TempoWorldEditor" not in packages
    # A directory under Source/ that is not a module. TempoModuleRules is a Build.cs in a folder of
    # its own; ThirdParty holds prebuilt libraries. Listing Source/ would collect both.
    assert "/Script/TempoModuleRules" not in packages
    assert "/Script/ThirdParty" not in packages
    # A plugin the project has to opt into, and one supplied from outside this tree.
    assert "/Script/TempoCoreROSBridge" not in packages, "TempoROSBridge is EnabledByDefault false"
    assert "/Script/TempoROS" not in packages, "TempoROS is a separate repository"


def test_an_opt_in_plugin_and_its_editor_modules_are_excluded(tmp_path):
    """The two exclusion rules, against a tree built to exercise them.

    The real plugin tree covers both today, but as policy rather than as mechanism: a plugin that
    became enabled-by-default would change what the test above asserts without saying anything
    about whether the rule still works.
    """
    def plugin(name, modules, enabled_by_default=None):
        directory = tmp_path / name
        (directory / "Source").mkdir(parents=True)
        descriptor = {"FriendlyName": name,
                      "Modules": [{"Name": m, "Type": t} for m, t in modules]}
        if enabled_by_default is not None:
            descriptor["EnabledByDefault"] = enabled_by_default
        (directory / (name + ".uplugin")).write_text(json.dumps(descriptor))

    plugin("Shipped", [("Shipped", "Runtime"), ("ShippedEditor", "Editor")])
    plugin("OptIn", [("OptIn", "Runtime")], enabled_by_default=False)

    assert subset_schema.tempo_packages(str(tmp_path)) == ["/Script/Shipped"]


def test_a_descriptor_with_a_trailing_comma_is_read():
    """Unreal's JSON parser tolerates a trailing comma, and several Tempo descriptors have one.

    Python's does not, so a plain `json.load` raises on TempoGeographic.uplugin -- which would make
    the published set silently depend on which descriptors happen to be strict.
    """
    descriptor = os.path.join(PLUGIN_ROOT, "TempoGeographic", "TempoGeographic.uplugin")
    with pytest.raises(json.JSONDecodeError):
        with open(descriptor, encoding="utf-8-sig") as f:
            json.load(f)
    assert subset_schema._read_uplugin(descriptor)["Modules"][0]["Name"] == "TempoGeographic"


# -- the subset ----------------------------------------------------------------------------------

def test_only_published_packages_are_kept(subset):
    narrowed, _ = subset
    kept = {cls["path"] for cls in narrowed["classes"]}
    assert "/Script/Engine.Widget" in kept
    assert "/Script/CoreUObject.Object" in kept
    assert not any(cls["package"] == "/Script/SomePlugin" for cls in narrowed["classes"])


def test_every_superclass_is_present(subset):
    """The subset's one structural invariant.

    Both generators read an unresolvable superclass as "this class is a root", silently, so a
    dangling path does not fail anything -- it just strips a class of every inherited member while
    the stubs still type-check. Asserted on the whole document rather than on the classes this
    fixture happens to reroot, because the next schema will have different ones.
    """
    narrowed, _ = subset
    kept = {cls["path"] for cls in narrowed["classes"]}
    dangling = [(cls["path"], cls["super"]) for cls in narrowed["classes"]
                if cls.get("super") and cls["super"] not in kept]
    assert dangling == []


def test_a_class_whose_superclass_was_dropped_is_rerooted(subset):
    narrowed, rerooted = subset
    by_path = {cls["path"]: cls for cls in narrowed["classes"]}

    assert by_path["/Script/Engine.Bridged"]["super"] == "/Script/Engine.Widget"
    assert rerooted["/Script/Engine.Bridged"] == (
        "/Script/Engine.Widget", ["/Script/SomePlugin.PluginBase"])


def test_rerooting_walks_the_whole_chain(subset):
    """Two hops outside the subset, so taking the first step up would still dangle."""
    narrowed, rerooted = subset
    by_path = {cls["path"]: cls for cls in narrowed["classes"]}

    assert by_path["/Script/Engine.DeepBridged"]["super"] == "/Script/Engine.Widget"
    new_super, skipped = rerooted["/Script/Engine.DeepBridged"]
    assert skipped == ["/Script/SomePlugin.PluginMiddle", "/Script/SomePlugin.PluginBase"]


def test_a_class_with_no_kept_ancestor_becomes_a_root(subset):
    narrowed, rerooted = subset
    by_path = {cls["path"]: cls for cls in narrowed["classes"]}

    assert by_path["/Script/Engine.Orphan"]["super"] == ""
    assert rerooted["/Script/Engine.Orphan"][0] == ""


def test_a_class_whose_superclass_is_kept_is_left_alone(subset):
    narrowed, rerooted = subset
    by_path = {cls["path"]: cls for cls in narrowed["classes"]}

    assert by_path["/Script/Engine.Widget"]["super"] == "/Script/CoreUObject.Object"
    assert "/Script/Engine.Widget" not in rerooted


def test_the_header_says_it_is_a_subset_and_carries_no_build_hash(subset):
    """A subset's whole-walk hash could only ever mismatch, so it is dropped rather than stale.

    `check_schema` compares one hash over the whole walk against the one the sim reports. No sim
    will ever report a subset's number, so leaving one in the header would make every published
    package look like the wrong build.
    """
    narrowed, _ = subset
    header = narrowed["header"]
    assert header["published_subset"] is True
    assert header["published_packages"] == list(FIXTURE_PACKAGES)
    assert "content_hash" not in header
    # What the stubs still need to say about themselves.
    assert header["engine_version"] == "5.8"
    assert header["schema_version"] == "3"


def test_the_header_records_what_was_rerooted(subset):
    """The per-class hashes still match the build, so nothing else says the view narrowed.

    A rerooted class can address the sim exactly as well as the project's own version of it -- it
    declares less, which is not something a hash over what it declares could show.
    """
    narrowed, _ = subset
    assert narrowed["header"]["rerooted"] == {
        "/Script/Engine.Bridged": "/Script/SomePlugin.PluginBase",
        "/Script/Engine.DeepBridged": "/Script/SomePlugin.PluginMiddle",
        "/Script/Engine.Orphan": "/Script/SomePlugin.Rootless",
    }


def test_the_per_class_hashes_survive(subset):
    """What a client compares when it holds only part of a build.

    Dropped along with the whole-walk hash would leave a published package with nothing to check
    at all, which is the failure this whole exercise is about.
    """
    narrowed, _ = subset
    for cls in narrowed["classes"]:
        assert cls.get("content_hash"), "{} lost its hash".format(cls["path"])
    for entry in narrowed["structs"] + narrowed["enums"]:
        assert entry.get("content_hash"), "{} lost its hash".format(entry["name"])


def test_types_nothing_kept_mentions_are_dropped(subset):
    narrowed, _ = subset
    # The widget's own structs, and the one FWidgetSettings reaches through a member.
    assert [struct["name"] for struct in narrowed["structs"]] == [
        "FWidgetLimits", "FWidgetSettings", "FWidgetTrim"]
    assert [enum["name"] for enum in narrowed["enums"]] == ["EWidgetMode"]


def test_an_empty_subset_is_refused(tmp_path):
    """Writing a package with no classes in it is never what was wanted.

    It means a module was renamed, or the schema is not the one it was taken for -- and the shape
    of that mistake is a wheel that imports and declares nothing, which no later step would
    complain about.
    """
    with open(FIXTURE) as f:
        schema = json.load(f)
    # Nothing in a published package. The engine list is hard-coded, so an empty plugin tree is
    # not enough to produce this.
    schema["classes"] = [cls for cls in schema["classes"] if cls["package"] == "/Script/SomePlugin"]
    document = tmp_path / "plugin_only.json"
    document.write_text(json.dumps(schema))

    out = tmp_path / "subset.json"
    result = subprocess.run(
        [sys.executable, os.path.join(PYTHON_DIR, "subset_schema.py"), str(document), str(out)],
        capture_output=True, text=True)
    assert result.returncode == 1, result.stdout
    assert "refusing to write an empty subset" in result.stderr
    assert not out.exists()


# -- the guarantee, through a type checker --------------------------------------------------------

@pytest.fixture(scope="module")
def generated_subset(tmp_path_factory, subset):
    """The subset, generated into an importable package."""
    narrowed, _ = subset
    out = tmp_path_factory.mktemp("subset")
    schema = out / "subset.json"
    schema.write_text(json.dumps(narrowed))
    subprocess.run([sys.executable, GENERATOR, str(schema), str(out / DIST)],
                   check=True, capture_output=True, text=True)
    return out


def test_a_rerooted_class_still_inherits_its_kept_ancestor(generated_subset, tmp_path):
    errors = check(generated_subset, """
import fixture_classes.Engine as E

def ok(b: E.Bridged) -> None:
    b.bBridged = True          # its own
    b.bEnabled = True          # Widget's, four hops up before the reroot
    b.Refresh()                # Widget's, likewise
""", tmp_path)
    assert errors == [], "a rerooted class must keep its kept ancestors: {}".format(errors)


def test_what_the_skipped_ancestor_declared_is_gone(generated_subset, tmp_path):
    """The honest cost of a subset: less, not wrong.

    `Tuning` is declared on the plugin class the subset dropped. A project's own package has it;
    this one must not pretend to.
    """
    errors = check(generated_subset, """
import fixture_classes.Engine as E

def gone(b: E.Bridged) -> None:
    b.Tuning = 1.0
""", tmp_path)
    assert errors, "a member only the dropped ancestor declared must not be reachable"


def test_a_dangling_superclass_would_lose_the_inherited_members(tmp_path_factory, tmp_path):
    """Why rerooting exists, asserted rather than described.

    Without it the kept classes are the same and the stubs still type-check -- the inherited half
    of the surface just quietly disappears. If this ever passes the same snippet as the test above,
    rerooting has stopped doing anything and nothing else would say so.
    """
    with open(FIXTURE) as f:
        schema = json.load(f)
    keep = {cls["path"] for cls in schema["classes"] if cls["package"] in FIXTURE_PACKAGES}
    from filter_schema import filter_schema
    narrowed = filter_schema(schema, keep)

    out = tmp_path_factory.mktemp("dangling")
    document = out / "dangling.json"
    document.write_text(json.dumps(narrowed))
    subprocess.run([sys.executable, GENERATOR, str(document), str(out / DIST)],
                   check=True, capture_output=True, text=True)

    errors = check(out, """
import fixture_classes.Engine as E

def ok(b: E.Bridged) -> None:
    b.bEnabled = True
""", tmp_path)
    assert errors, "a dangling superclass should have cost Bridged its inherited members"


def test_a_reference_outside_the_subset_degrades_to_object(generated_subset, tmp_path):
    """A property pointing at a dropped class is typed `Object`, not dropped and not `Any`.

    25 members of 7,652 in the real subset are like this. `Object` is honest -- the handle is still
    usable by path -- where `Any` would quietly accept anything and dropping it would make correct
    code an error.
    """
    stub = (generated_subset / DIST / "Engine.pyi").read_text()
    assert "Outsider: _unreal.Object" in stub, [
        line for line in stub.splitlines() if "Outsider" in line]

    errors = check(generated_subset, """
import fixture_classes.Engine as E

def reaching(d: E.DeepBridged) -> None:
    d.Outsider.Tuning = 1.0
""", tmp_path)
    assert errors, "a dropped class's members must not be reachable through the Object it became"


# -- generated into the published wheel -----------------------------------------------------------

@pytest.fixture(scope="module")
def nested(tmp_path_factory, subset):
    """The subset generated where it ships: inside another package, under its engine version.

    `tempo_sim.UE.v5_8` rather than a distribution of its own (TYPED_WORLD_API_PLAN.md section
    16.4), and `v5_8` rather than `5_8` because an identifier cannot begin with a digit.
    """
    narrowed, _ = subset
    out = tmp_path_factory.mktemp("nested")
    schema = out / "subset.json"
    schema.write_text(json.dumps(narrowed))
    package = out / DIST / "UE" / "v5_8"
    (out / DIST).mkdir(parents=True, exist_ok=True)
    (out / DIST / "__init__.py").write_text("")
    (out / DIST / "UE").mkdir(parents=True, exist_ok=True)
    (out / DIST / "UE" / "__init__.py").write_text("")
    subprocess.run([sys.executable, GENERATOR, str(schema), str(package),
                    "--package={}.UE.v5_8".format(DIST)],
                   check=True, capture_output=True, text=True)
    return out


def test_a_nested_package_imports_itself_by_its_full_name(nested):
    """The emitted modules import each other, and a directory name is not the dotted name.

    Left to the directory this would emit `import v5_8.enums`, which resolves to nothing from
    anywhere -- and the failure would be an ImportError at runtime rather than anything a type
    checker reports.
    """
    stub = (nested / DIST / "UE" / "v5_8" / "Engine.pyi").read_text()
    assert "{}.UE.v5_8.enums".format(DIST) in stub, [
        line for line in stub.splitlines() if "import" in line]


def test_the_published_classes_type_check_where_they_ship(nested, tmp_path):
    errors = check(nested, """
from fixture_classes.UE.v5_8.Engine import Widget

def ok(w: Widget) -> None:
    w.bEnabled = True
    print(w.Offset.X)
""", tmp_path)
    assert errors == [], "the published classes should type-check: {}".format(errors)

    errors = check(nested, """
from fixture_classes.UE.v5_8.Engine import Widget

def drift(w: Widget) -> None:
    w.bEnabledd = True
""", tmp_path)
    assert errors, "the guarantee has to survive being nested"
