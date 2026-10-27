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

from test_codegen import DIST, GENERATOR, check

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
                                          "callable": f.get("callable", True),
                                          "positional": f.get("positional", True)}
                              for f in cls["functions"]},
                "components": {c["name"]: c["class"] for c in cls["components"]},
            }
            for cls in schema["classes"]
        },
        "structs": {
            struct["name"]: {"content_hash": "BUILD-" + struct["name"].upper(),
                             "value_expressible": struct["value_expressible"],
                             "members": {m["name"]: member(m["readable"], m["writable"],
                                                           m["value_expressible"])
                                         for m in struct["members"]}}
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
    assert stats["super_disagreements"] == []
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


def test_struct_and_function_expressibility_come_from_the_build(schema, surface):
    """Whether a struct travels whole decides which functions are offered, so it is the build's call.

    An editor can hold a member a build does not -- a WITH_EDITORONLY_DATA one -- and that member
    alone can make a struct inexpressible in one and not the other.
    """
    surface["structs"]["FWidgetSettings"]["value_expressible"] = False
    surface["classes"]["/Script/Engine.Widget"]["functions"]["Configure"]["positional"] = False
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["flags_corrected"] == 2
    settings = next(s for s in schema["structs"] if s["name"] == "FWidgetSettings")
    assert settings["value_expressible"] is False
    configure = next(f for f in widget(schema)["functions"] if f["name"] == "Configure")
    assert configure["positional"] is False


def test_callability_comes_from_the_build(schema, surface):
    """`callable` and `positional` together decide whether a function is offered, so both are the
    build's call -- a function the build cannot call is withheld however the editor saw it."""
    surface["classes"]["/Script/Engine.Widget"]["functions"]["Refresh"]["callable"] = False
    stats = filter_schema.narrow_to_surface(schema, surface)

    assert stats["flags_corrected"] == 1
    refresh = next(f for f in widget(schema)["functions"] if f["name"] == "Refresh")
    assert refresh["callable"] is False


def test_the_superclass_comes_from_the_build(schema, surface, tmp_path, capsys):
    """A superclass is the one field whose drift strips every inherited member at once.

    The two should agree, since a packaged build is compiled from the same headers, so a
    disagreement means the schema and the report are not of one build -- which is worth saying
    by name rather than counting, and which does not stop the build's view from being taken.
    """
    bridged = next(cls for cls in schema["classes"] if cls["path"] == "/Script/Engine.Bridged")
    surface["classes"]["/Script/Engine.Bridged"]["super"] = "/Script/Engine.Widget"
    stats = filter_schema.narrow_to_surface(copy.deepcopy(schema), surface)
    assert stats["super_disagreements"] == [
        ("/Script/Engine.Bridged", "/Script/SomePlugin.PluginBase", "/Script/Engine.Widget")]

    schema_path, report_path, out_path = (tmp_path / name for name in ("s.json", "r.json", "o.json"))
    schema_path.write_text(json.dumps(schema))
    report_path.write_text(json.dumps({
        "classes": [cls["path"] for cls in schema["classes"]], "surface": surface,
        "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "BUILD"}}))
    assert filter_schema.main(["filter_schema.py", str(schema_path), str(report_path),
                               str(out_path)]) == 0
    assert "/Script/Engine.Bridged: /Script/SomePlugin.PluginBase -> /Script/Engine.Widget" in \
        capsys.readouterr().err
    filtered = json.loads(out_path.read_text())
    assert next(cls for cls in filtered["classes"]
                if cls["path"] == bridged["path"])["super"] == "/Script/Engine.Widget"


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


def test_a_class_whose_superclass_the_build_lacks_is_rerooted(tmp_path, schema, surface):
    """The other half of the same invariant: nothing the package declares may dangle either.

    A schema and a report taken from the same build keep every superclass by construction; a stale
    schema beside a fresh report need not. Both generators read a superclass they cannot resolve as
    "this class is a root", so a dangling one strips every inherited member without a type error.
    So such a class is pointed at its nearest kept ancestor, and the header records it.
    """
    gone = "/Script/SomePlugin.PluginBase"
    del surface["classes"][gone]
    schema_path, report_path, out_path = (tmp_path / name for name in ("s.json", "r.json", "o.json"))
    schema_path.write_text(json.dumps(schema))
    report_path.write_text(json.dumps({
        "classes": [cls["path"] for cls in schema["classes"] if cls["path"] != gone],
        "surface": surface,
        "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "BUILD"}}))

    assert filter_schema.main(["filter_schema.py", str(schema_path), str(report_path),
                               str(out_path)]) == 0
    filtered = json.loads(out_path.read_text())
    by_path = {cls["path"]: cls for cls in filtered["classes"]}

    assert gone not in by_path
    # Bridged derived from the dropped class directly; PluginMiddle did too, and DeepBridged, which
    # derives from PluginMiddle, is left alone because its own superclass is still there.
    assert by_path["/Script/Engine.Bridged"]["super"] == "/Script/Engine.Widget"
    assert by_path["/Script/SomePlugin.PluginMiddle"]["super"] == "/Script/Engine.Widget"
    assert by_path["/Script/Engine.DeepBridged"]["super"] == "/Script/SomePlugin.PluginMiddle"
    assert filtered["header"]["rerooted"] == {
        "/Script/Engine.Bridged": gone, "/Script/SomePlugin.PluginMiddle": gone}
    # The invariant itself, over the whole document.
    assert [(cls["path"], cls["super"]) for cls in filtered["classes"]
            if cls.get("super") and cls["super"] not in by_path] == []


def test_a_rerooted_class_keeps_what_its_kept_ancestors_declare(tmp_path, schema):
    """Why rerooting exists, asserted rather than described, through a type checker.

    The same narrowing twice, once rerooted and once left dangling. The kept classes are the same
    and both sets of stubs type-check -- but without rerooting the inherited half of the surface
    quietly disappears. If the dangling case ever passes, rerooting has stopped mattering and
    nothing else would say so.
    """
    gone = "/Script/SomePlugin.PluginBase"
    keep = {cls["path"] for cls in schema["classes"] if cls["path"] != gone}
    snippet = """
import fixture_classes.Engine as E

def ok(b: E.Bridged) -> None:
    b.bBridged = True          # its own
    b.bEnabled = True          # Widget's, above the dropped class
    b.Refresh()                # Widget's, likewise
"""

    def generate(document, name):
        out = tmp_path / name
        out.mkdir()
        (out / "schema.json").write_text(json.dumps(document))
        subprocess.run([sys.executable, GENERATOR, str(out / "schema.json"), str(out / DIST)],
                       check=True, capture_output=True, text=True)
        return out

    dangling = filter_schema.filter_schema(schema, keep)
    rerooted = copy.deepcopy(dangling)
    filter_schema.reroot(rerooted, {cls["path"]: cls for cls in schema["classes"]})

    errors = check(generate(rerooted, "rerooted"), snippet, tmp_path)
    assert errors == [], "a rerooted class must keep its kept ancestors: {}".format(errors)
    assert check(generate(dangling, "dangling"), snippet, tmp_path), \
        "a dangling superclass should have cost Bridged its inherited members"


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
                "header": {"schema_version": "3", "engine_version": "5.8",
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


def test_a_report_sharing_no_class_with_the_schema_is_refused(tmp_path):
    """A schema and a report of different builds intersect in nothing, and the shape of that
    mistake -- a stale Saved/TempoSchema beside a fresh packaged_class_paths.json -- is a package
    that imports and declares nothing, which no later step would complain about."""
    reported = {"classes": ["/Script/Engine.NotInThisSchema"],
                "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "BUILD"}}
    report = tmp_path / "reported.json"
    report.write_text(json.dumps(reported))
    out = tmp_path / "filtered.json"

    result = subprocess.run(
        [sys.executable, os.path.join(PYTHON_DIR, "filter_schema.py"), FIXTURE,
         str(report), str(out)],
        capture_output=True, text=True)
    assert result.returncode == 1, result.stdout
    assert "refusing to write an empty schema" in result.stderr
    assert not out.exists()


def test_a_type_reachable_only_from_a_dropped_member_is_dropped(tmp_path):
    """The closure has to run again after the members are narrowed, not only before.

    The first pass closes over the *editor's* members, so a struct reached only from a member the
    build does not have survives it -- and is then declared by a package the sim will not confirm.
    38 of them in TempoSample, all editor-only: FExportHLODAssetsParams, FBakingConfiguration,
    FAnimDataModelNotifPayload. The live `check_schema` test is what caught it, which is the
    per-entry check earning its place on its first outing.
    """
    schema = {
        "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "EDITOR"},
        "classes": [{
            "name": "Widget", "path": "/Script/Engine.Widget", "super": "", "package": "/Script/Engine",
            "abstract": False, "blueprint": False, "content_hash": "EDITOR-WIDGET",
            "components": [], "functions": [],
            "properties": [
                {"name": "Shipped", "kind": "struct", "type": "FShipped", "struct": "FShipped",
                 "readable": True, "writable": True, "value_expressible": False},
                {"name": "EditorOnly", "kind": "struct", "type": "FEditorOnly",
                 "struct": "FEditorOnly",
                 "readable": True, "writable": True, "value_expressible": False},
            ],
        }],
        "structs": [{"name": "FShipped", "content_hash": "E1", "members": []},
                    {"name": "FEditorOnly", "content_hash": "E2", "members": []}],
        "enums": [],
    }
    reported = {
        "classes": ["/Script/Engine.Widget"],
        "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "BUILD"},
        "surface": {
            "classes": {"/Script/Engine.Widget": {
                "content_hash": "BUILD-WIDGET", "super": "",
                "properties": {"Shipped": member(value_expressible=False)},
                "functions": {}, "components": {}}},
            # The build reports only the types its own members refer to, which is why re-closing
            # over the narrowed members lands on the same set.
            "structs": {"FShipped": {"content_hash": "B1", "members": {}}},
            "enums": {},
        },
    }
    document = tmp_path / "schema.json"
    document.write_text(json.dumps(schema))
    report = tmp_path / "reported.json"
    report.write_text(json.dumps(reported))
    out = tmp_path / "filtered.json"

    result = subprocess.run(
        [sys.executable, os.path.join(PYTHON_DIR, "filter_schema.py"), str(document),
         str(report), str(out)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

    filtered = json.loads(out.read_text())
    assert [s["name"] for s in filtered["structs"]] == ["FShipped"]


# -- the asset catalog -----------------------------------------------------------------------------

def _report(assets):
    return [{"path": path, "class": cls} for path, cls in assets]


def test_the_catalog_keeps_what_was_cooked_and_nothing_else(schema):
    classes = {cls["path"]: cls for cls in schema["classes"]}
    classes["/Script/CoreUObject.Class"] = {"path": "/Script/CoreUObject.Class", "super": ""}
    classes["/Script/Engine.BlueprintGeneratedClass"] = {
        "path": "/Script/Engine.BlueprintGeneratedClass", "super": "/Script/CoreUObject.Class"}
    classes["/Script/Engine.BlueprintCore"] = {"path": "/Script/Engine.BlueprintCore", "super": ""}
    classes["/Script/Engine.Blueprint"] = {"path": "/Script/Engine.Blueprint",
                                           "super": "/Script/Engine.BlueprintCore"}

    kept = filter_schema.catalog(_report([
        ("/Game/Meshes/SM_Rock.SM_Rock", "/Script/Engine.MeshThing"),
        # What moving an asset leaves behind: keeping it would keep the old name compiling.
        ("/Game/Old/SM_Rock.SM_Rock", "/Script/CoreUObject.ObjectRedirector"),
        # A Blueprint is a class, already in the class surface.
        ("/Game/BP_Truck.BP_Truck_C", "/Script/Engine.BlueprintGeneratedClass"),
        ("/Game/BP_Truck.BP_Truck", "/Script/Engine.Blueprint"),
        # World Partition's machinery, which nothing addresses by path.
        ("/Game/Maps/Open/_Generated_/0ABC.Open", "/Script/Engine.World"),
        ("/Game/__ExternalActors__/Maps/Open/1/AB/XYZ.XYZ", "/Script/Engine.World"),
        ("/Game/Maps/Open.Open", "/Script/Engine.World"),
    ]), classes)
    assert [asset["path"] for asset in kept] == ["/Game/Maps/Open.Open", "/Game/Meshes/SM_Rock.SM_Rock"]


def test_the_catalog_comes_from_the_build(tmp_path, schema, surface):
    """An editor schema has no catalog; the filtered one takes its assets from the report."""
    schema_path, report_path, out_path = (tmp_path / name for name in ("s.json", "r.json", "o.json"))
    schema.pop("assets", None)
    schema_path.write_text(json.dumps(schema))
    report_path.write_text(json.dumps({
        "classes": [cls["path"] for cls in schema["classes"]],
        "surface": surface,
        "assets": _report([("/Game/A.A", "/Script/Engine.MeshThing")]),
        "header": {"schema_version": "3", "engine_version": "5.8", "content_hash": "BUILD"},
    }))
    assert filter_schema.main(["filter_schema.py", str(schema_path), str(report_path), str(out_path)]) == 0
    assert json.loads(out_path.read_text())["assets"] == [{"path": "/Game/A.A", "class": "/Script/Engine.MeshThing"}]
