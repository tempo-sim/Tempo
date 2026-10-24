# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the generated typed class surface.

These test the guarantee rather than the text. A golden-output test would notice that the emitted
bytes changed; what matters is whether a type checker rejects the things it has to reject, because
that -- not readability -- is why the stubs exist. So each case generates from a fixture schema and
asserts what Pyright says about a snippet.

Needs neither a packaged sim nor a wheel: `pip install pyright` brings its own Node. Skipped, not
failed, when Pyright is absent, since the rest of the suite should still run without it.

    Scripts/TestPythonAPI.sh codegen        (once wired into the runner)
    pytest -m codegen Tests/Python
"""

import json
import os
import shutil
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GENERATOR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "gen_py_classes.py")
RUNTIME_API = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "API")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

DIST = "fixture_classes"


def pyright_executable():
    """Pyright, from PATH or from a venv beside the interpreter running the tests."""
    found = shutil.which("pyright")
    if found:
        return found
    candidate = os.path.join(os.path.dirname(sys.executable), "pyright")
    return candidate if os.path.exists(candidate) else None


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    """The fixture schema, generated into an importable package."""
    out = tmp_path_factory.mktemp("generated")
    package = out / DIST
    subprocess.run([sys.executable, GENERATOR, FIXTURE, str(package)], check=True,
                   capture_output=True, text=True)
    return out


def check(generated, source, tmp_path):
    """Run Pyright over one snippet. Returns its error diagnostics for that file."""
    pyright = pyright_executable()
    if pyright is None:
        pytest.skip("pyright is not installed (pip install pyright)")

    snippet = tmp_path / "snippet.py"
    snippet.write_text(source)
    config = tmp_path / "pyrightconfig.json"
    config.write_text(json.dumps({
        "extraPaths": [str(generated), RUNTIME_API],
        "typeCheckingMode": "standard",
    }))

    result = subprocess.run([pyright, "--project", str(tmp_path), "--outputjson", str(snippet)],
                            capture_output=True, text=True)
    report = json.loads(result.stdout)
    return [d for d in report["generalDiagnostics"]
            if d["severity"] == "error" and d["file"].endswith("snippet.py")]


def test_generates_the_expected_files(generated):
    package = generated / DIST
    # Three files per module: identity, types, and the metadata the runtime builds requests from.
    for name in ("Engine.py", "Engine.pyi", "Engine.meta.json", "enums.py", "structs.py"):
        assert (package / name).exists(), "{} was not generated".format(name)

    metadata = json.loads((package / "Engine.meta.json").read_text())
    widget = metadata["/Script/Engine.Widget"]
    assert widget["super"] == "/Script/CoreUObject.Object", "inheritance must be recoverable"
    # The runtime cannot infer a Value variant from a Python type -- a str could be six things --
    # so the metadata has to name it.
    assert widget["properties"]["bEnabled"]["variant"] == "bool_value"
    assert widget["properties"]["Tags"]["variant"] == "string_array_value"
    assert widget["properties"]["bEnabled"]["writable"] is True
    assert widget["properties"]["Offset"]["writable"] is False


def test_identity_file_has_no_annotations(generated):
    """The .py carries class identity only. Annotations there would cost a dict per class."""
    identity = (generated / DIST / "Engine.py").read_text()
    assert "__tempo_class__" in identity
    assert "bEnabled" not in identity, "the identity file must not carry members"


def test_components_are_typed_as_their_own_class(generated, tmp_path):
    errors = check(generated, """
import fixture_classes.Engine as E

def ok(w: E.Widget) -> None:
    w.Mesh.Visible = True
""", tmp_path)
    assert errors == [], "a component's own members should type-check: {}".format(errors)


def test_a_reference_is_typed_as_the_class_it_points_at(generated, tmp_path):
    """A property holding an actor, asset, component or class is typed as that class.

    The schema calls the key `class`; the proto calls the same thing `class_path`, and the
    generator read the proto's name. The lookup therefore never matched and the "pointee is
    outside this walk" fallback always won, so all 4,977 reference-valued members and parameters in
    TempoSample were typed as a bare `Object`. Nothing failed -- the stub was merely useless, which
    is the failure mode §9 exists to catch, so it is asserted on the type rather than on the text.
    """
    stub = (generated / DIST / "Engine.pyi").read_text()
    assert "Target: MeshThing" in stub, "a reference should name its class, not Object: {}".format(
        [line for line in stub.splitlines() if "Target" in line])

    errors = check(generated, """
import fixture_classes.Engine as E

def ok(w: E.Widget) -> None:
    w.Target.Visible = True
""", tmp_path)
    assert errors == [], "a reference's own members should type-check: {}".format(errors)

    errors = check(generated, """
import fixture_classes.Engine as E

def bad(w: E.Widget) -> None:
    w.Target.Nope = True
""", tmp_path)
    assert errors, "a member the pointee does not have must still be an error"


def test_a_component_is_declared_once(generated):
    """A Blueprint component variable is both a component template and the property holding it.

    Emitting both puts the name in the class body twice, and the second declaration is the one a
    type checker believes -- so the typed handle would lose to a bare Object, making correct code
    an error. The runtime looks for a property first, so it would answer the read with the
    component's name as a string rather than a handle. The component accessor has to be the only
    declaration, in the stub and in the metadata alike.
    """
    package = generated / DIST
    stub = (package / "Engine.pyi").read_text()
    assert stub.count("def Mesh(") == 1, "Mesh should be declared once"
    assert "Mesh: " not in stub, "Mesh must not also be declared as a plain attribute"

    widget = json.loads((package / "Engine.meta.json").read_text())["/Script/Engine.Widget"]
    assert widget["components"]["Mesh"] == "/Script/Engine.MeshThing"
    assert "Mesh" not in widget["properties"], "the runtime would serve the property instead"


@pytest.mark.parametrize("case,source", [
    ("assigning to a component", "w.Mesh = None"),
    ("a component whose class is absent", "w.Gone.Visible = True"),
    ("an unnameable component", "w.Has_Space.Visible = True"),
    ("a member the component does not have", "w.Mesh.NoSuchThing = True"),
])
def test_component_misuse_is_a_type_error(generated, tmp_path, case, source):
    errors = check(generated, """
import fixture_classes.Engine as E

def drift(w: E.Widget) -> None:
    {}
""".format(source), tmp_path)
    assert errors, "{} should be a type error".format(case)


def test_valid_usage_type_checks(generated, tmp_path):
    errors = check(generated, """
import fixture_classes.Engine as E
import fixture_classes.enums as enums

def ok(w: E.Widget) -> None:
    w.bEnabled = True
    w.Mode = enums.EWidgetMode.Advanced
    w.Tags = ["a", "b"]
    print(w.Offset.X, w.Settings.Scale)
    if w.Resize(4):
        w.Refresh()
""", tmp_path)
    assert errors == [], "valid usage should type-check cleanly: {}".format(errors)


def test_struct_values_type_check(generated, tmp_path):
    errors = check(generated, """
import fixture_classes.Engine as E
import fixture_classes.structs as structs
import tempo_sim.unreal as _unreal

def ok(w: E.Widget) -> None:
    w.Settings = structs.FWidgetSettings(Scale=2.0, Inner=structs.FWidgetTrim(Offset=_unreal.Vector()))
    w.Settings.Scale = 3.0
    result: structs.FWidgetSettings = w.Configure(structs.FWidgetSettings(Scale=1.0))
    print(result.Scale, result.Label)
    w.History = [structs.FWidgetSettings()]
    w.Configure(w.Settings)
    copy: structs.FWidgetSettings = _unreal.value_of(w.Settings)
    print(copy.Inner.Offset.X, w.Limits.Floor)
""", tmp_path)
    assert errors == [], "struct values should type-check cleanly: {}".format(errors)


def test_a_deferred_read_is_typed_by_what_it_reads(generated, tmp_path):
    errors = check(generated, """
import fixture_classes.Engine as E
from tempo_sim import world

def ok(w: E.Widget) -> None:
    with world.batch() as b:
        enabled = b.get(lambda: w.bEnabled)
        grown = b.call(lambda: w.Resize(4))
    flag: bool = enabled.value
    resized: bool = grown.value
""", tmp_path)
    assert errors == [], "a deferred read should carry its type: {}".format(errors)

    for source in ("s: str = b.get(lambda: w.bEnabled).value", "b.get(lambda: w.bEnabledd)"):
        errors = check(generated, """
import fixture_classes.Engine as E
from tempo_sim import world

def drift(w: E.Widget) -> None:
    with world.batch() as b:
        {}
""".format(source), tmp_path)
        assert errors, "{} should be a type error".format(source)


def test_out_parameters_are_typed_by_name(generated, tmp_path):
    """With out parameters a method returns every result keyed by name, and the stub has to say so.

    It used to say only the return value -- `-> bool` -- while the runtime handed back a dict, so a
    caller doing what the stub said got a dict where a bool was promised.
    """
    errors = check(generated, """
import fixture_classes.Engine as E

def ok(w: E.Widget) -> None:
    results = w.Measure()
    moved: bool = results["ReturnValue"]
    scale = results["Measured"].Scale
""", tmp_path)
    assert errors == [], "named results should type-check: {}".format(errors)

    for source in ('w.Measure()["Tally"]', "b: bool = w.Measure()"):
        errors = check(generated, """
import fixture_classes.Engine as E

def drift(w: E.Widget) -> None:
    {}
""".format(source), tmp_path)
        assert errors, "{} should be a type error".format(source)


@pytest.mark.parametrize("case,source", [
    ("a member the struct lacks", "structs.FWidgetSettings(Scal=1.0)"),
    ("a positional member", "structs.FWidgetSettings(1.0)"),
    ("a read-only member in the constructor", 'structs.FWidgetSettings(Label="x")'),
    ("a member of the wrong type", 'structs.FWidgetSettings(Scale="big")'),
    ("the wrong struct as an argument", "w.Configure(structs.FWidgetTrim())"),
    ("assigning a struct that cannot travel", "w.Limits = structs.FWidgetLimits()"),
    ("a function returning a struct that cannot travel", "w.GetLimits()"),
    ("a function taking a struct that cannot travel", "w.ApplyLimits(structs.FWidgetLimits())"),
    ("a member nothing can read", "print(w.Waypoints)"),
])
def test_struct_drift_is_a_type_error(generated, tmp_path, case, source):
    errors = check(generated, """
import fixture_classes.Engine as E
import fixture_classes.structs as structs

def drift(w: E.Widget) -> None:
    {}
""".format(source), tmp_path)
    assert errors, "{} should be a type error, but Pyright accepted it".format(case)


@pytest.mark.parametrize("case,source", [
    # A property removed in a new release.
    ("removed property", "w.bEnabledd = True"),
    # A property that became read-only. This is why read-only members are emitted as setter-less
    # properties rather than plain annotations: a plain annotation is writable.
    ("read-only property", "w.Offset = _unreal.Vector()"),
    # A property whose type changed.
    ("wrong type", 'w.bEnabled = "yes"'),
    # A function removed, or its signature changed.
    ("removed function", "w.Refreshh()"),
    ("wrong argument type", 'w.Resize("four")'),
    # A type the API cannot carry must be absent, never typed Any -- Any would pass silently.
    ("unsupported property is absent", "w.Doomed = 1"),
    # A name Python cannot spell is likewise absent rather than quietly mangled.
    ("unnameable property is absent", "setattr(w, 'Has Spaces', True); w.Has_Spaces = True"),
    # A function the server will not invoke is not offered.
    ("uncallable function is absent", "w.Latent()"),
    # Any undeclared attribute at all. This guards the whole guarantee: a catch-all anywhere in the
    # visible hierarchy, or a base that resolves to Unknown, makes every one of these pass.
    ("undeclared attribute", "w.AnythingAtAll = 1"),
])
def test_drift_is_a_type_error(generated, tmp_path, case, source):
    errors = check(generated, """
import fixture_classes.Engine as E
import tempo_sim.unreal as _unreal

def drift(w: E.Widget) -> None:
    {}
""".format(source), tmp_path)
    assert errors, "{} should be a type error, but Pyright accepted it".format(case)


def test_enum_members_are_checked(generated, tmp_path):
    errors = check(generated, """
import fixture_classes.enums as enums

def gone() -> enums.EWidgetMode:
    return enums.EWidgetMode.Removed
""", tmp_path)
    assert errors, "a removed enumerator should be a type error"
