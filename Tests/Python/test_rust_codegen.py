# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the generated typed Rust class surface.

The same shape as test_codegen.py, and for the same reason: a golden-output test would notice that
the emitted bytes changed, but what matters is whether the compiler rejects what it has to reject.
So each case generates from the same fixture schema and asserts what rustc says about a snippet.

The snippet is its own crate depending on the generated one, which is the position that matters:
the orphan rule and `Deref` coercion both behave differently inside the generating crate than
outside it, and outside is where a scenario lives.

Needs no packaged sim and no wheel, only cargo. Skipped, not failed, when cargo is absent. The
cargo target directory is shared and kept under Saved/, so only the first run pays for building
tempo-sim's dependencies.

    Scripts/TestPythonAPI.sh codegen
    pytest -m codegen Tests/Python
"""

import os
import shutil
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GENERATOR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python", "gen_rust_classes.py")
RUST_API = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Rust", "API")
TARGET_DIR = os.path.join(PLUGIN_ROOT, "Saved", "RustCodegenTarget")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

CRATE = "fixture-classes"
CRATE_IMPORT = "fixture_classes"

PREAMBLE = """#![allow(unused_imports, unused_variables, dead_code, unused_must_use, non_snake_case)]
use fixture_classes::classes::core_u_object::Object;
use fixture_classes::classes::engine::{MeshThing, Widget};
use fixture_classes::enums::EWidgetMode;
use fixture_classes::structs::{FWidgetSettings, FWidgetTrim};
use tempo_sim::unreal::{Class, ObjectRef, Vector};
use tempo_sim::{world, TempoError};

fn takes_mesh(_m: &MeshThing) {}

fn main() -> Result<(), TempoError> {
    let widget = Widget(ObjectRef::actor("a"));
    let mesh = MeshThing(ObjectRef::actor("b"));
"""

EPILOGUE = """
    Ok(())
}
"""


def cargo_executable():
    found = shutil.which("cargo")
    if found:
        return found
    candidate = os.path.expanduser("~/.cargo/bin/cargo")
    return candidate if os.path.exists(candidate) else None


def crate_version():
    """tempo-sim's version, which the generated crate pins its dependency to."""
    with open(os.path.join(RUST_API, "Cargo.toml")) as manifest:
        for line in manifest:
            if line.startswith("version"):
                return line.split("=", 1)[1].strip().strip('"')
    raise AssertionError("tempo-sim's Cargo.toml has no version")


@pytest.fixture(scope="module")
def workspace(tmp_path_factory):
    """The fixture schema generated into a crate, with a snippet crate ready to compile.

    `tempo-sim` arrives as a symlink named exactly what the generated manifest's `../tempo-sim`
    expects, so the layout under test is the one `Packaged/API/Rust` arranges.
    """
    cargo = cargo_executable()
    if cargo is None:
        pytest.skip("cargo is not installed")

    root = tmp_path_factory.mktemp("rust")
    subprocess.run(
        [sys.executable, GENERATOR, FIXTURE, str(root / CRATE),
         "--version={}".format(crate_version())],
        check=True, capture_output=True, text=True)
    os.symlink(RUST_API, str(root / "tempo-sim"))

    snippet = root / "snippet"
    (snippet / "src").mkdir(parents=True)
    (snippet / "Cargo.toml").write_text(
        '[package]\nname = "snippet"\nversion = "0.0.0"\nedition = "2021"\n'
        '[dependencies]\n'
        'tempo-sim = {{ path = "../tempo-sim" }}\n'
        '{} = {{ path = "../{}" }}\n'.format(CRATE_IMPORT.replace("_", "-"), CRATE))
    return root


def check(workspace, body):
    """Compile one snippet body. Returns rustc's errors for that file."""
    cargo = cargo_executable()
    snippet = workspace / "snippet"
    (snippet / "src" / "main.rs").write_text(PREAMBLE + body + EPILOGUE)

    environment = dict(os.environ, CARGO_TARGET_DIR=TARGET_DIR)
    result = subprocess.run(
        [cargo, "build", "--quiet", "--message-format=short"],
        cwd=str(snippet), capture_output=True, text=True, env=environment)
    return [line for line in result.stderr.splitlines()
            if line.startswith("src/main.rs") and ": error" in line]


# -- the surface exists and is typed -------------------------------------------------------------

def test_generates_the_expected_files(workspace):
    crate = workspace / CRATE
    for name in ("Cargo.toml", "src/lib.rs", "src/enums.rs", "src/structs.rs",
                 "src/hierarchy.rs", "src/classes.rs", "src/classes/engine.rs"):
        assert (crate / name).exists(), "{} was not generated".format(name)


def test_a_property_reads_and_writes(workspace):
    assert check(workspace, """
    let enabled: bool = widget.b_enabled()?;
    widget.set_b_enabled(true)?;
    let offset: Vector = widget.offset()?;
""") == []


def test_an_inherited_property_resolves_through_deref(workspace):
    """Name is declared on Object, and Widget derives from it.

    This is the whole reason the generated classes are a Deref chain. Nothing in Widget's own impl
    mentions Name.
    """
    assert "fn name(" not in (workspace / CRATE / "src" / "classes" / "engine.rs").read_text(), \
        "Name must come from Object, not be copied into Widget"
    assert check(workspace, """
    let _: String = widget.name()?;
""") == []


def test_a_derived_handle_is_accepted_where_a_base_is_wanted(workspace):
    assert check(workspace, """
    fn takes_object(_o: &Object) {}
    takes_object(&widget);
""") == []


def test_a_component_is_typed_as_its_own_class(workspace):
    assert check(workspace, """
    let _: bool = widget.mesh().visible()?;
""") == []


def test_a_reference_is_typed_as_the_class_it_points_at(workspace):
    """The Rust half of the `class` vs `class_path` bug: a reference must name its class.

    Note the two `?`: unlike Python, where attribute access is implicit I/O and `w.Target.Visible`
    chains, reading a reference here is a call that can fail, so the handle has to be unwrapped
    before anything is asked of it.
    """
    assert check(workspace, """
    let _: bool = widget.target()?.visible()?;
""") == []


def test_both_sync_and_async_forms_exist(workspace):
    assert check(workspace, """
    let _ = async {
        let _: bool = widget.b_enabled_async().await?;
        widget.set_b_enabled_async(true).await?;
        Ok::<(), TempoError>(())
    };
""") == []


def test_a_struct_property_is_addressed_a_member_at_a_time(workspace):
    assert check(workspace, """
    let _: f64 = widget.settings().scale()?;
""") == []


def test_struct_values_compile(workspace):
    """A struct that travels whole: built, assigned, passed, returned and loaded."""
    assert check(workspace, """
    let settings = FWidgetSettings {
        scale: Some(2.0),
        inner: Some(FWidgetTrim { offset: Some(Vector::default()) }),
        ..Default::default()
    };
    widget.set_settings(settings.clone())?;
    let back: FWidgetSettings = widget.configure(settings)?;
    let _: Option<String> = back.label;
    let copy: FWidgetSettings = widget.settings().load()?;
    widget.settings().store(copy)?;
    widget.settings().inner().set_offset(Vector::default())?;
    widget.set_history(vec![FWidgetSettings::default()])?;
    let _: Vec<FWidgetSettings> = widget.history()?;
    let _: f64 = widget.limits().floor()?;
""") == []


def test_a_function_is_callable(workspace):
    assert check(workspace, """
    widget.refresh()?;
    let _: bool = widget.resize(7)?;
""") == []


def test_a_setter_names_the_argument_it_sends(workspace):
    """A BlueprintSetter write has to carry the setter's own parameter name.

    CallFunction refuses an unnamed argument -- "Every arg in a CallFunction request must have a
    name" -- so this is not cosmetic: every write to one of the 306 properties with a setter failed
    until the generator resolved it. Asserted on the emitted call, because the failure is a runtime
    one that no amount of type checking would catch.
    """
    engine = (workspace / CRATE / "src" / "classes" / "engine.rs").read_text()
    assert 'set_via("SetScaleFactor", "InScale"' in engine, \
        "the setter and its argument both have to be named: {}".format(
            [line.strip() for line in engine.splitlines() if "set_via" in line])


def test_a_property_whose_setter_cannot_be_named_is_read_only(workspace):
    """Writing it directly would skip the setter, which is what the accessor path prevents."""
    engine = (workspace / CRATE / "src" / "classes" / "engine.rs").read_text()
    assert "fn untouchable(" in engine, "it should still be readable"
    assert "fn set_untouchable(" not in engine, "it must not be writable"
    assert check(workspace, """
    let _: bool = widget.untouchable()?;
""") == []
    assert check(workspace, """
    widget.set_untouchable(true)?;
"""), "a property whose setter cannot be named must not be writable"


# -- drift must not compile ----------------------------------------------------------------------

@pytest.mark.parametrize("case,body", [
    ("a misspelled property", "let _ = widget.b_enabeld()?;"),
    ("a misspelled property, async twin", "let _ = widget.b_enabeld_async();"),
    ("a write to a read-only property", "widget.set_offset(Vector::default())?;"),
    ("a write to a read-only property, async twin", "widget.set_offset_async(Vector::default());"),
    ("a wrong value type", "widget.set_b_enabled(1)?;"),
    ("a property of an unrelated class", "let _ = widget.visible()?;"),
    ("an unrelated class where this one is wanted", "takes_mesh(&widget);"),
    ("a member the pointee does not have", "let _ = widget.target()?.nope()?;"),
    ("a variant the enum does not have", "widget.set_mode(EWidgetMode::Nope)?;"),
    ("watching a property the class lacks", "let _ = widget.watch_visible()?;"),
    ("a function the class does not have", "widget.vanish()?;"),
    ("a function called with the wrong arity", "let _ = widget.resize()?;"),
    ("discovery by a type that is not a class", "let _ = world::get_actors::<u32>()?;"),
    ("a struct member the struct lacks", "let _ = widget.settings().nope()?;"),
    ("a struct value field the struct lacks",
     "let _ = FWidgetSettings { scal: Some(1.0), ..Default::default() };"),
    ("a struct value field of the wrong type",
     "let _ = FWidgetSettings { scale: Some(\"big\".to_string()), ..Default::default() };"),
    ("the wrong struct as an argument", "let _ = widget.configure(FWidgetTrim::default())?;"),
    ("a value type for a struct that cannot travel",
     "let _ = fixture_classes::structs::FWidgetLimits::default();"),
    ("assigning a struct that cannot travel", "widget.set_limits(Default::default())?;"),
    ("loading a struct that cannot travel", "let _ = widget.limits().load()?;"),
    ("a function returning a struct that cannot travel", "let _ = widget.get_limits()?;"),
    ("a function taking a struct that cannot travel", "widget.apply_limits()?;"),
    ("a container nothing can read", "let _ = widget.waypoints()?;"),
])
def test_drift_does_not_compile(workspace, case, body):
    """Each of these is what a release is expected to break, and must break the build.

    §9.1 has no Rust analogue to guard: there is no catch-all to hide from the compiler, so an
    undeclared method is an error with nothing that could suppress it. These assert that the
    generated shape does not accidentally reintroduce one -- a blanket impl, or a Deref reaching
    somewhere it should not.
    """
    assert check(workspace, "    " + body), "{} must not compile".format(case)


def test_the_escape_hatch_is_explicit(workspace):
    """The string-keyed API stays reachable, and stays visibly unchecked.

    A root class gets no Deref, so ObjectRef's methods are not reachable on a handle by accident.
    Taking the escape hatch means naming the field.
    """
    assert check(workspace, """
    let _: i32 = widget.0.get("WhateverThisIs")?;
""") == []
    assert check(workspace, """
    let _: i32 = widget.get("WhateverThisIs")?;
"""), "ObjectRef's string API must not be reachable without naming the field"
