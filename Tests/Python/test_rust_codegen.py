# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the generated reflected Rust API.

The same shape as test_codegen.py, and for the same reason: a golden-output test would notice that
the emitted bytes changed, but what matters is whether the compiler rejects what it has to reject.
So each case generates from the same fixture schema and asserts what rustc says about a snippet.

The classes are generated where a project's are, as the `reflected` module of a host crate whose
manifest and lib.rs come from gen_project_package.py, as a project's do. The snippet is its own
crate depending on that one, which is the position that matters: the orphan rule and `Deref`
coercion both behave differently inside the generating crate than outside it, and outside is where
a scenario lives.

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
PYTHON_DIR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python")
GENERATOR = os.path.join(PYTHON_DIR, "gen_rust_classes.py")
RUST_API = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Rust", "API")
TARGET_DIR = os.path.join(PLUGIN_ROOT, "Saved", "RustCodegenTarget")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

CRATE = "fixture-project"
CRATE_IMPORT = "fixture_project"

PREAMBLE = """#![allow(unused_imports, unused_variables, dead_code, unused_must_use, non_snake_case)]
use fixture_project::reflected::classes::core_u_object::Object;
use fixture_project::reflected::classes::engine::{MeshThing, Widget};
use fixture_project::reflected::enums::EWidgetMode;
use fixture_project::reflected::structs::{FWidgetSettings, FWidgetTrim};
use tempo_sim::unreal::{Class, ObjectRef, FVector};
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


def _sync(source, destination):
    """Make `destination` hold what `source` does, touching only files whose content differs.

    So that cargo, which goes by modification time, rebuilds only what changed between runs.
    """
    source, destination = str(source), str(destination)
    wanted = set()
    for directory, _, files in os.walk(source):
        relative = os.path.relpath(directory, source)
        for name in files:
            path = os.path.normpath(os.path.join(relative, name))
            wanted.add(path)
            target = os.path.join(destination, path)
            with open(os.path.join(directory, name), "rb") as f:
                content = f.read()
            if os.path.exists(target):
                with open(target, "rb") as f:
                    if f.read() == content:
                        continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as f:
                f.write(content)
    for directory, _, files in os.walk(destination):
        for name in files:
            path = os.path.normpath(os.path.relpath(os.path.join(directory, name), destination))
            if path not in wanted:
                os.remove(os.path.join(directory, name))


def tempo_sim_copy(name):
    """tempo-sim's sources, at a fixed path under Saved/ so cargo's cache survives between runs."""
    copy = os.path.join(PLUGIN_ROOT, "Saved", "RustCodegen", name, "tempo-sim")
    _sync(os.path.join(RUST_API, "src"), os.path.join(copy, "src"))
    with open(os.path.join(RUST_API, "Cargo.toml"), "rb") as f:
        manifest = f.read()
    target = os.path.join(copy, "Cargo.toml")
    if not os.path.exists(target) or open(target, "rb").read() != manifest:
        with open(target, "wb") as f:
            f.write(manifest)
    return copy


def make_workspace(root, tempo_sim):
    """A host crate holding the fixture's types, and a snippet crate depending on it.

    `tempo-sim` arrives as a symlink beside the host crate, so the layout under test is the one
    `Packaged/API/Rust` arranges, and both crates name it by the same `../tempo-sim` -- one path, so
    cargo sees one tempo-sim rather than two whose types do not unify.
    """
    os.symlink(tempo_sim, str(root / "tempo-sim"))
    host = root / CRATE
    subprocess.run(
        [sys.executable, GENERATOR, FIXTURE, str(host / "src" / "reflected"),
         "--root=crate::reflected"],
        check=True, capture_output=True, text=True)

    sys.path.insert(0, PYTHON_DIR)
    try:
        import gen_project_package as package
    finally:
        sys.path.remove(PYTHON_DIR)
    package.write_project_cargo_toml(host, CRATE, root / "tempo-sim",
                                     package.read_version(root / "tempo-sim" / "Cargo.toml"))
    package.write_project_lib_rs(host / "src")

    snippet = root / "snippet"
    (snippet / "src").mkdir(parents=True)
    (snippet / "Cargo.toml").write_text(
        '[package]\nname = "snippet"\nversion = "0.0.0"\nedition = "2021"\n'
        '[dependencies]\n'
        'tempo-sim = {{ path = "../tempo-sim" }}\n'
        '{} = {{ path = "../{}" }}\n'.format(CRATE_IMPORT.replace("_", "-"), CRATE))
    return root


@pytest.fixture(scope="module")
def workspace(tmp_path_factory):
    """The fixture's types in a host crate."""
    if cargo_executable() is None:
        pytest.skip("cargo is not installed")
    if not os.path.exists(os.path.join(RUST_API, "src", "lib.rs")):
        pytest.skip("tempo-sim's generated Rust sources are not in the tree: build with "
                    "TEMPO_GEN_RUST_API=1, or run Scripts/TestPythonAPI.sh codegen against a "
                    "package that has the crate, which takes them from it")
    return make_workspace(tmp_path_factory.mktemp("rust"), tempo_sim_copy("plain"))


def run(workspace, body):
    """Compile one snippet body and run it. Returns (stdout, stderr, exit status)."""
    assert check(workspace, body) == []
    environment = dict(os.environ, CARGO_TARGET_DIR=TARGET_DIR)
    result = subprocess.run([cargo_executable(), "run", "--quiet"], cwd=str(workspace / "snippet"),
                            capture_output=True, text=True, env=environment)
    return result.stdout, result.stderr, result.returncode


def check(workspace, body):
    """Compile one snippet body. Returns rustc's errors for that file."""
    cargo = cargo_executable()
    snippet = workspace / "snippet"
    (snippet / "src" / "main.rs").write_text(PREAMBLE + body + EPILOGUE)

    environment = dict(os.environ, CARGO_TARGET_DIR=TARGET_DIR)
    result = subprocess.run(
        [cargo, "build", "--quiet", "--message-format=short"],
        cwd=str(snippet), capture_output=True, text=True, env=environment)
    errors = [line for line in result.stderr.splitlines()
              if line.startswith("src/main.rs") and ": error" in line]
    # A generated crate that does not build never gets as far as the snippet, which would
    # otherwise read as a snippet with no errors.
    assert errors or result.returncode == 0, result.stderr
    return errors


# -- the surface exists and is typed -------------------------------------------------------------

def test_generates_the_expected_files(workspace):
    crate = workspace / CRATE
    for name in ("Cargo.toml", "src/lib.rs", "src/reflected/mod.rs", "src/reflected/enums.rs",
                 "src/reflected/structs.rs", "src/reflected/hierarchy.rs",
                 "src/reflected/classes.rs", "src/reflected/classes/engine.rs"):
        assert (crate / name).exists(), "{} was not generated".format(name)


def test_the_host_crate_declares_the_classes_behind_a_default_feature(workspace):
    """A crate with classes and no protos: no proto module or dependencies, `reflected` on."""
    crate = workspace / CRATE
    lib = (crate / "src" / "lib.rs").read_text()
    assert '#[cfg(feature = "reflected")]\npub mod reflected;' in lib
    assert "pub mod proto;" not in lib
    manifest = (crate / "Cargo.toml").read_text()
    assert 'default = ["reflected"]' in manifest
    assert "tonic" not in manifest


def test_a_property_reads_and_writes(workspace):
    assert check(workspace, """
    let enabled: bool = widget.b_enabled()?;
    widget.set_b_enabled(true)?;
    let offset: FVector = widget.offset()?;
""") == []


def test_an_inherited_property_resolves_through_deref(workspace):
    """Name is declared on Object, and Widget derives from it.

    This is the whole reason the generated classes are a Deref chain. Nothing in Widget's own impl
    mentions Name.
    """
    engine = workspace / CRATE / "src" / "reflected" / "classes" / "engine.rs"
    assert "fn name(" not in engine.read_text(), \
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
        inner: Some(FWidgetTrim { offset: Some(FVector::default()) }),
        ..Default::default()
    };
    widget.set_settings(settings.clone())?;
    let back: FWidgetSettings = widget.configure(settings)?;
    let _: Option<String> = back.label;
    let copy: FWidgetSettings = widget.settings().load()?;
    widget.settings().store(copy)?;
    widget.settings().inner().set_offset(FVector::default())?;
    widget.set_history(vec![FWidgetSettings::default()])?;
    let _: Vec<FWidgetSettings> = widget.history()?;
    let _: f64 = widget.limits().floor()?;
""") == []


def test_a_deferred_read_is_typed_by_what_it_reads(workspace):
    assert check(workspace, """
    world::batch(|| {
        widget.set_b_enabled(true)?;
        let enabled: world::Deferred<bool> = world::defer(|| widget.b_enabled())?;
        let resized: world::Deferred<bool> = world::defer(|| widget.resize(4))?;
        Ok(())
    })?;
""") == []
    assert check(workspace, """
    world::batch(|| {
        let _: world::Deferred<String> = world::defer(|| widget.b_enabled())?;
        Ok(())
    })?;
"""), "a deferred read must keep the type of what it reads"


def test_struct_members_have_async_forms(workspace):
    assert check(workspace, """
    let _ = async {
        let _: f64 = widget.settings().scale_async().await?;
        widget.settings().set_scale_async(2.0).await?;
        widget.settings().inner().set_offset_async(FVector::default()).await?;
        Ok::<(), TempoError>(())
    };
""") == []


def test_an_async_batch_is_send_and_typed(workspace):
    """Its future holds only what was queued, so a spawned task can await it."""
    assert check(workspace, """
    fn assert_send<T: Send>(_: &T) {}
    let mut enabled = None;
    let sent = world::batch_async(|| {
        widget.set_b_enabled(true)?;
        widget.settings().set_scale(2.0)?;
        enabled = Some(world::defer(|| widget.b_enabled())?);
        Ok(())
    });
    assert_send(&sent);
    let enabled: world::Deferred<bool> = enabled.unwrap();
    assert_send(&enabled);
""") == []


def test_an_immediate_read_inside_an_async_batch_is_refused(workspace):
    """It would have to block, which panics inside a runtime; refused before anything is sent."""
    stdout, stderr, status = run(workspace, """
    let sent = world::batch_async(|| {
        widget.set_b_enabled(true)?;
        let _ = widget.b_enabled()?;
        Ok(())
    });
    let outcome = tempo_sim::context::RUNTIME.block_on(sent);
    println!("{}", outcome.err().map(|e| e.to_string()).unwrap_or_default());
""")
    assert status == 0, stderr
    assert "made directly inside batch_async" in stdout
    assert "a.bEnabled" in stdout, "the refusal names the read"


def test_a_batch_opened_inside_another_joins_it(workspace):
    """The inner one sends nothing of its own -- it would go out ahead of the outer's earlier ops.
    Nothing listens on port 1, so only the outer batch, which does send, fails."""
    stdout, stderr, status = run(workspace, """
    tempo_sim::set_server("localhost", 1);
    let mut inner = None;
    let outer = world::batch(|| {
        widget.set_b_enabled(true)?;
        inner = Some(world::batch(|| widget.set_b_enabled(false)).is_ok());
        Ok(())
    });
    println!("{} {}", inner.unwrap(), outer.is_err());
""")
    assert status == 0, stderr
    assert stdout.strip() == "true true"


def test_the_classes_are_registered_before_main(workspace):
    """So check_schema works first thing. The snippet's handles are built directly, which registers
    nothing, so only the startup registration can have done it."""
    stdout, stderr, status = run(workspace, """
    println!("{}", tempo_sim::unreal::hierarchies().len());
    let _ = &widget;
""")
    assert status == 0, stderr
    assert stdout.strip() == "1"


def test_the_asset_catalog_compiles(workspace):
    assert check(workspace, """
    use fixture_project::reflected::assets::game::widgets::W_Main;
    use tempo_sim::unreal::AssetRef;
    widget.set_skin(&W_Main)?;
    // A MeshThing asset is an Object asset, through the same Deref chain as the classes.
    widget.set_any_asset(&W_Main)?;
    use fixture_project::reflected::assets::game::maps::showroom::Lighting;
    let _: bool = widget.reskin(&Lighting)?;
    let _: AssetRef<MeshThing> = widget.skin()?;
    // A map and the folder beside it, which Rust keeps in separate namespaces.
    let _ = &fixture_project::reflected::assets::game::maps::Showroom;
    let _ = &fixture_project::reflected::assets::engine::basic::_9Grid;
    // The escape hatch: a path from data, untyped, visibly so.
    widget.set_skin(&AssetRef::from_path("/Game/FromAConfigFile"))?;
""") == []


def test_a_subsystem_is_reached_by_its_class(workspace):
    assert check(workspace, """
    let subsystem: Widget = world::get_subsystem::<Widget>()?;
    let enabled: bool = subsystem.b_enabled()?;
    assert_eq!(subsystem.as_object().subsystem_class(), Widget::PATH);
    assert_eq!(subsystem.as_object().actor_name(), "");
""") == []


def test_every_enumerator_has_a_variant(workspace):
    """Symbol-only enumerators are numbered, and names that mangle alike are all kept.

    `<` once mangled to `_`, which is not a Rust identifier, so the whole crate failed to build;
    and of two enumerators mangling to one name the second was dropped, leaving a value the sim
    can report that the client could not decode.
    """
    assert check(workspace, """
    use fixture_project::reflected::enums::EWidgetComparison;
    use tempo_sim::unreal::UnrealEnum;
    let named = [
        (EWidgetComparison::Enumerator0, "=="),
        (EWidgetComparison::Enumerator1, "<"),
        (EWidgetComparison::Enumerator1_2, "Enumerator1"),
        (EWidgetComparison::Less_Than, "Less Than"),
        (EWidgetComparison::Less_Than_4, "Less_Than"),
        (EWidgetComparison::Greater, "Greater"),
    ];
    for (variant, authored) in named {
        assert_eq!(variant.unreal_name(), authored);
        assert_eq!(EWidgetComparison::from_unreal_name(authored)?, variant);
    }
""") == []


def test_a_read_only_propertys_set_function_is_kept(workspace):
    """A read-only property has no setter, so its Set<Name> function is the way to change it.

    Reserving the setter's name regardless shadowed UStaticMeshComponent::SetStaticMesh -- the one
    way to set a static mesh -- behind a property that would never use the name.
    """
    assert check(workspace, """
    let _: bool = widget.set_frame(&fixture_project::reflected::assets::game::widgets::W_Main)?;
    let _ = widget.frame()?;
""") == []


@pytest.mark.parametrize("case,body", [
    ("an asset that was renamed",
     "widget.set_skin(&fixture_project::reflected::assets::game::widgets::W_Mian)?;"),
    ("an asset of the wrong class",
     "widget.set_skin(&fixture_project::reflected::assets::game::widgets::trims::T_Gold)?;"),
    ("a bare path", 'widget.set_skin("/Game/Widgets/W_Main")?;'),
    ("two assets that collide, both left out",
     "let _ = &fixture_project::reflected::assets::game::widgets::Dup_One;"),
])
def test_asset_drift_does_not_compile(workspace, case, body):
    assert check(workspace, "    " + body), "{} must not compile".format(case)


def test_a_function_is_callable(workspace):
    assert check(workspace, """
    widget.refresh()?;
    let _: bool = widget.resize(7)?;
""") == []


def test_the_world_mirror_takes_handles_and_si(workspace):
    """world is tempo_world given handles: the same functions, SI arguments, typed results where
    something is made."""
    assert check(workspace, """
    use tempo_sim::proto::tempo_world::{CollisionChannel, ComponentFrame};
    world::destroy_actor(&widget)?;
    let light: MeshThing = world::add_component::<MeshThing>(&widget, "Light", Some(mesh.as_object()), None, "")?;
    let meshes: Vec<MeshThing> = world::get_components::<MeshThing>(&widget)?;
    world::set_actor_location(&widget, world::Vector::default(), Some(mesh.as_object()))?;
    world::set_component_rotation(&mesh, world::Rotation::default(), true)?;
    let _ = world::get_component_transform(&mesh, ComponentFrame::CfActor, None, "")?;
    let _ = world::get_current_actor_state(&widget, false)?;
    let _ = world::raycast(world::Vector::default(), world::Vector::default(),
                           CollisionChannel::CcVisibility, &[widget.as_object(), mesh.as_object()])?;
    let _ = world::stream_actor_state(&widget, false)?;
    let _ = async { world::set_actor_scale3d_async(&widget, world::Vector::default()).await };
""") == []
    assert check(workspace, """
    world::set_actor_location(&widget, FVector::default(), None)?;
"""), "world takes SI types, as tempo_world does, not Unreal's"


def test_a_setter_names_the_argument_it_sends(workspace):
    """A BlueprintSetter write has to carry the setter's own parameter name.

    CallFunction refuses an unnamed argument -- "Every arg in a CallFunction request must have a
    name" -- so this is not cosmetic: every write to one of the 306 properties with a setter failed
    until the generator resolved it. Asserted on the emitted call, because the failure is a runtime
    one that no amount of type checking would catch.
    """
    engine = (workspace / CRATE / "src" / "reflected" / "classes" / "engine.rs").read_text()
    assert 'set_via("SetScaleFactor", "InScale"' in engine, \
        "the setter and its argument both have to be named: {}".format(
            [line.strip() for line in engine.splitlines() if "set_via" in line])


def test_a_property_whose_setter_cannot_be_named_is_read_only(workspace):
    """Writing it directly would skip the setter, which is what the accessor path prevents."""
    engine = (workspace / CRATE / "src" / "reflected" / "classes" / "engine.rs").read_text()
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
    ("a write to a read-only property", "widget.set_offset(FVector::default())?;"),
    ("a write to a read-only property, async twin",
     "widget.set_offset_async(FVector::default());"),
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
     "let _ = fixture_project::reflected::structs::FWidgetLimits::default();"),
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


# -- conversions -----------------------------------------------------------------------------------

def test_conversions_apply_the_servers_rule(workspace):
    # TempoConversion.h's rule: cm -> m with Y negated for a length, degrees -> radians with pitch
    # and yaw negated for a rotation, and a direction's handedness alone.
    stdout, stderr, status = run(workspace, """
    use tempo_sim::conversion::*;
    use tempo_sim::proto::tempo_core as si;
    use tempo_sim::unreal::{FRotator, FTransform, FVector2D};

    println!("{} {}", ue_to_si(&250.0), si_to_ue(&2.5));
    let v: si::Vector = ue_to_si(&FVector { x: 100.0, y: 50.0, z: -25.0 });
    println!("{} {} {}", v.x, v.y, v.z);
    let v2: si::Vector2D = ue_to_si(&FVector2D { x: 100.0, y: 50.0 });
    println!("{} {}", v2.x, v2.y);
    let r: si::Rotation = ue_to_si(&FRotator { pitch: 90.0, yaw: 45.0, roll: 180.0 });
    println!("{} {} {}", r.r == std::f64::consts::PI, r.p == -std::f64::consts::FRAC_PI_2,
             r.y == -std::f64::consts::FRAC_PI_4);
    let d: si::Vector = ue_direction_to_si(&FVector { x: 0.0, y: 1.0, z: 0.0 });
    println!("{} {} {}", d.x, d.y, d.z);

    // Scale 1: TempoCore's Transform carries none, so a round trip comes back with 1.
    let pose = FTransform {
        rotation: quat_of(&FRotator { pitch: 5.0, yaw: 6.0, roll: 4.0 }),
        translation: FVector { x: 1.0, y: 2.0, z: 3.0 },
        ..FTransform::default()
    };
    let back: FTransform = si_to_ue(&ue_to_si(&pose));
    let turned = rotator_of(&back.rotation);
    let close = |a: f64, b: f64| (a - b).abs() < 1e-9;
    println!("{}", close(back.translation.y, 2.0) && close(turned.pitch, 5.0)
             && close(turned.yaw, 6.0) && close(turned.roll, 4.0) && back.scale3d.x == 1.0);
    println!("{}", si_direction_to_ue(&d) == FVector { x: 0.0, y: 1.0, z: 0.0 });
    // A message that leaves a field unset means the zero it is on the wire.
    println!("{:?}", si_to_ue(&si::Transform::default()) == FTransform::default());
    // Unreal's own formulas, at a pole too, where Unreal folds roll into yaw.
    let q = quat_of(&FRotator { pitch: 0.0, yaw: 90.0, roll: 0.0 });
    println!("{}", close(q.z, 0.5f64.sqrt()) && close(q.w, 0.5f64.sqrt()) && q.x == 0.0);
    let pole = rotator_of(&quat_of(&FRotator { pitch: 90.0, yaw: 30.0, roll: 0.0 }));
    println!("{}", close(pole.pitch, 90.0) && close(pole.yaw, 30.0) && pole.roll == 0.0);
""")
    assert status == 0, stderr
    assert stdout.split("\n")[:10] == ["2.5 250", "1 -0.5 -0.25", "1 -0.5", "true true true",
                                       "0 -1 0", "true", "true", "true", "true", "true"]


@pytest.mark.parametrize("case,source", [
    ("a value with no SI counterpart",
     "let _ = tempo_sim::conversion::ue_to_si(&tempo_sim::unreal::FColor::default());"),
    ("converting an SI value the wrong way",
     "let _ = tempo_sim::conversion::ue_to_si("
     "&tempo_sim::proto::tempo_core::Vector::default());"),
    ("converting an Unreal value the wrong way",
     "let _ = tempo_sim::conversion::si_to_ue(&FVector::default());"),
    ("a conversion typed as another quantity",
     "let _: FVector = tempo_sim::conversion::si_to_ue("
     "&tempo_sim::proto::tempo_core::Rotation::default());"),
    ("the old prefixed name", "let _ = tempo_sim::unreal::UnrealVector::default();"),
    ("the old unprefixed name", "let _ = tempo_sim::unreal::Vector::default();"),
])
def test_conversion_misuse_does_not_compile(workspace, case, source):
    assert check(workspace, source), "{} should not compile, but it did".format(case)
