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


def test_the_plugin_root_option_takes_both_spellings(tmp_path):
    """`--plugin-root <dir>` and `--plugin-root=<dir>`, as the usage line promises.

    Only the `=` form used to parse; the other took `--plugin-root` as a flag and the directory as
    a third positional, and crashed listing a directory named `--plugin-root`.
    """
    plugins = tmp_path / "plugins"
    (plugins / "Shipped").mkdir(parents=True)
    (plugins / "Shipped" / "Shipped.uplugin").write_text(json.dumps(
        {"Modules": [{"Name": "Shipped", "Type": "Runtime"}]}))

    for spelling in (["--plugin-root", str(plugins)], ["--plugin-root={}".format(plugins)]):
        out = tmp_path / "subset.json"
        result = subprocess.run(
            [sys.executable, os.path.join(PYTHON_DIR, "subset_schema.py"), FIXTURE, str(out)]
            + spelling, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "/Script/Shipped" in json.loads(out.read_text())["header"]["published_packages"]
        out.unlink()


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


def test_a_published_subset_has_no_asset_catalog(subset):
    """What a project cooks is its own, engine content included."""
    narrowed, _ = subset
    assert "assets" not in narrowed


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
    """The subset generated where it ships: inside another package, as `tempo_sim.reflected` is.

    Not a distribution of its own (TYPED_WORLD_API_PLAN.md section 16.4).
    """
    narrowed, _ = subset
    out = tmp_path_factory.mktemp("nested")
    schema = out / "subset.json"
    schema.write_text(json.dumps(narrowed))
    package = out / DIST / "reflected"
    (out / DIST).mkdir(parents=True, exist_ok=True)
    (out / DIST / "__init__.py").write_text("")
    subprocess.run([sys.executable, GENERATOR, str(schema), str(package),
                    "--package={}.reflected".format(DIST)],
                   check=True, capture_output=True, text=True)
    return out


def test_a_nested_package_imports_itself_by_its_full_name(nested):
    """The emitted modules import each other, and a directory name is not the dotted name.

    Left to the directory this would emit `import reflected.enums`, which resolves to nothing from
    anywhere -- and the failure would be an ImportError at runtime rather than anything a type
    checker reports.
    """
    stub = (nested / DIST / "reflected" / "Engine.pyi").read_text()
    assert "{}.reflected.enums".format(DIST) in stub, [
        line for line in stub.splitlines() if "import" in line]


def test_the_published_classes_type_check_where_they_ship(nested, tmp_path):
    errors = check(nested, """
from fixture_classes.reflected.Engine import Widget

def ok(w: Widget) -> None:
    w.bEnabled = True
    print(w.Offset.X)
""", tmp_path)
    assert errors == [], "the published classes should type-check: {}".format(errors)

    errors = check(nested, """
from fixture_classes.reflected.Engine import Widget

def drift(w: Widget) -> None:
    w.bEnabledd = True
""", tmp_path)
    assert errors, "the guarantee has to survive being nested"


# -- a project's types re-export the published ones (section 16.5) --------------------------------

def _shared(schema, published):
    sys.path.insert(0, PYTHON_DIR)
    try:
        from gen_classes_common import shared_with
    finally:
        sys.path.remove(PYTHON_DIR)
    return shared_with(schema, published)


def _fixture():
    with open(FIXTURE) as f:
        return json.load(f)


def test_what_a_build_publishes_unchanged_is_shared(subset):
    narrowed, _ = subset
    classes, structs, enums = _shared(_fixture(), narrowed)
    assert {"/Script/CoreUObject.Object", "/Script/Engine.MeshThing",
            "/Script/Engine.Widget"} <= classes
    # Rerooted by the subset, so its chain differs though its own hash does not.
    assert "/Script/Engine.Bridged" not in classes
    assert "/Script/SomePlugin.PluginBase" not in classes, "not published at all"
    assert structs == {"FWidgetLimits", "FWidgetSettings", "FWidgetTrim"}
    # EWidgetComparison is not published: nothing the subset keeps mentions it.
    assert enums == {"EWidgetMode"}


def test_a_changed_struct_unshares_whatever_carries_it(subset):
    """FWidgetSettings holds an FWidgetTrim, and Widget an FWidgetSettings: a struct the project
    redefines has to be the one its own types hand back."""
    import copy

    narrowed, _ = subset
    published = copy.deepcopy(narrowed)
    for struct in published["structs"]:
        if struct["name"] == "FWidgetTrim":
            struct["content_hash"] = "A-DIFFERENT-TRIM"
    classes, structs, _ = _shared(_fixture(), published)
    assert structs == {"FWidgetLimits"}
    assert "/Script/Engine.Widget" not in classes
    assert "/Script/Engine.MeshThing" in classes, "nothing MeshThing has mentions the trim"


def test_a_class_mentioning_an_unpublished_class_is_still_shared(subset):
    """Its member is a bare Object in both packages, which is the lesser cost.

    Unsharing it instead was tried, and broke the case the asset catalog exists for: StaticMesh
    mentions a class the subset lacks, so the project defined its own -- and the shared
    StaticMeshComponent's setter then took a StaticMesh the project's catalog did not hand out.
    """
    import copy

    narrowed, _ = subset
    schema, published = _fixture(), copy.deepcopy(narrowed)
    mention = {"name": "Plugin", "kind": "actor", "class": "/Script/SomePlugin.PluginBase",
               "readable": True, "writable": True}
    for document in (schema, published):
        for cls in document["classes"]:
            if cls["path"] == "/Script/Engine.MeshThing":
                cls["properties"] = cls.get("properties", []) + [dict(mention)]
    classes, _, _ = _shared(schema, published)
    assert "/Script/Engine.MeshThing" in classes


PROJECT = "fixture_project"


@pytest.fixture(scope="module")
def reexporting(tmp_path_factory, subset):
    """The published types, and a project's generated against them.

    The published copy is altered as a different release would be: Widget and FWidgetSettings hash
    differently, so the project has to define its own. MeshThing and Object match; Bridged was
    rerooted by the subset, so its chain differs even though its own hash does not.
    """
    import copy

    narrowed, _ = subset
    published = copy.deepcopy(narrowed)
    for cls in published["classes"]:
        if cls["path"] == "/Script/Engine.Widget":
            cls["content_hash"] = "A-DIFFERENT-WIDGET"
    for struct in published["structs"]:
        if struct["name"] == "FWidgetSettings":
            struct["content_hash"] = "A-DIFFERENT-SETTINGS"

    out = tmp_path_factory.mktemp("reexporting")
    published_schema = out / "published.json"
    published_schema.write_text(json.dumps(published))
    (out / DIST).mkdir()
    (out / DIST / "__init__.py").write_text("")
    subprocess.run([sys.executable, GENERATOR, str(published_schema), str(out / DIST / "reflected"),
                    "--package={}.reflected".format(DIST)],
                   check=True, capture_output=True, text=True)
    (out / PROJECT).mkdir()
    (out / PROJECT / "__init__.py").write_text("")
    subprocess.run([sys.executable, GENERATOR, FIXTURE, str(out / PROJECT / "reflected"),
                    "--package={}.reflected".format(PROJECT),
                    "--published={}".format(published_schema),
                    "--published-package={}.reflected".format(DIST)],
                   check=True, capture_output=True, text=True)
    return out


def _read_json(path):
    with open(path) as f:
        return json.load(f)


def test_what_the_published_types_share_is_re_exported(reexporting):
    engine = (reexporting / PROJECT / "reflected" / "Engine.py").read_text()
    stub = (reexporting / PROJECT / "reflected" / "Engine.pyi").read_text()
    line = "from {}.reflected.Engine import MeshThing as MeshThing".format(DIST)
    assert line in engine and line in stub
    # Defined here: changed (Widget), rerooted by the subset (Bridged), or not published at all.
    for name in ("Widget", "Bridged"):
        assert "class {}(".format(name) in engine, name
    plugin = reexporting / PROJECT / "reflected" / "SomePlugin.py"
    assert "class PluginBase(" in plugin.read_text()


def test_a_re_exported_entry_is_left_to_the_published_package(reexporting):
    """One registration per class: the published index answers for what it defines."""
    project = reexporting / PROJECT / "reflected"
    index = _read_json(project / "index.json")
    assert "/Script/Engine.MeshThing" not in index
    assert "/Script/CoreUObject.Object" not in index
    assert "/Script/Engine.Widget" in index

    hashes = _read_json(project / "hashes.json")
    assert "/Script/Engine.MeshThing" not in hashes["classes"]
    assert "FWidgetTrim" not in hashes["structs"] and "FWidgetSettings" in hashes["structs"]
    assert "EWidgetMode" not in hashes["enums"]

    assert "import {}.reflected".format(DIST) in (project / "__init__.py").read_text(), \
        "the project's types have to register the published ones they lean on"


def test_structs_and_enums_are_re_exported_too(reexporting):
    project = reexporting / PROJECT / "reflected"
    structs = (project / "structs.pyi").read_text()
    assert "from {}.reflected.structs import FWidgetTrim as FWidgetTrim".format(DIST) in structs
    assert "class FWidgetSettings(" in structs
    assert "from {}.reflected.enums import EWidgetMode as EWidgetMode".format(DIST) in \
        (project / "enums.py").read_text()


def test_a_shared_class_is_one_type_to_a_checker(reexporting, tmp_path):
    """The point of re-exporting: a helper written against the published type takes the project's."""
    errors = check(reexporting, """
import {dist}.reflected.Engine as published
import {project}.reflected.Engine as project

def helper(mesh: published.MeshThing) -> None: ...

def ok(mesh: project.MeshThing) -> None:
    helper(mesh)
""".format(dist=DIST, project=PROJECT), tmp_path)
    assert errors == [], "a re-exported class should be the published one: {}".format(errors)

    errors = check(reexporting, """
import {dist}.reflected.Engine as published
import {project}.reflected.Engine as project

def helper(widget: published.Widget) -> None: ...

def diverged(widget: project.Widget) -> None:
    helper(widget)
""".format(dist=DIST, project=PROJECT), tmp_path)
    assert errors, "a class the release describes differently is the project's own type"

