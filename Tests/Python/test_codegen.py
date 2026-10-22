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
