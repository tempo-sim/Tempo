# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the generated typed Rust class surface.

The same shape as test_codegen.py, and for the same reason: a golden-output test would notice that
the emitted bytes changed, but what matters is whether the compiler rejects what it has to reject.
So each case generates from the same fixture schema and asserts what rustc says about a snippet.

The classes are generated where a project's are, as the `types` module of a host crate whose manifest
and lib.rs come from gen_project_package.py, as a project's do. The snippet is its own crate
depending on that one, which is the position that matters: the orphan rule and `Deref` coercion
both behave differently inside the generating crate than outside it, and outside is where a
scenario lives.

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
use fixture_project::types::classes::core_u_object::Object;
use fixture_project::types::classes::engine::{MeshThing, Widget};
use fixture_project::types::enums::EWidgetMode;
use fixture_project::types::structs::{FWidgetSettings, FWidgetTrim};
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


def _sync(source, destination, skip=()):
    """Make `destination` hold what `source` does, touching only files whose content differs.

    So that cargo, which goes by modification time, rebuilds only what changed between runs.
    """
    source, destination = str(source), str(destination)
    wanted = set()
    for directory, subdirectories, files in os.walk(source):
        relative = os.path.relpath(directory, source)
        subdirectories[:] = [d for d in subdirectories
                             if os.path.normpath(os.path.join(relative, d)) not in skip]
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
            if path not in wanted and not any(path.startswith(s + os.sep) for s in skip):
                os.remove(os.path.join(directory, name))


def tempo_sim_copy(name, published=None, scratch=None):
    """tempo-sim's sources, at a fixed path under Saved/ so cargo's cache survives between runs.

    A copy rather than the crate itself, because what `src/types` holds is the test's to decide:
    nothing, or the published types generated from `published`. The in-tree crate holds whatever
    the last package left there.
    """
    copy = os.path.join(PLUGIN_ROOT, "Saved", "RustCodegen", name, "tempo-sim")
    _sync(os.path.join(RUST_API, "src"), os.path.join(copy, "src"), skip=("types",))
    with open(os.path.join(RUST_API, "Cargo.toml"), "rb") as f:
        manifest = f.read()
    target = os.path.join(copy, "Cargo.toml")
    if not os.path.exists(target) or open(target, "rb").read() != manifest:
        with open(target, "wb") as f:
            f.write(manifest)
    types = os.path.join(copy, "src", "types")
    if published is None:
        if os.path.exists(types):
            shutil.rmtree(types)
    else:
        with open(os.path.join(RUST_API, "src", "lib.rs")) as f:
            assert "pub mod types;" in f.read(), \
                "tempo-sim's lib.rs predates the published types; rebuild to regenerate it"
        staged = os.path.join(str(scratch), "published-types")
        subprocess.run([sys.executable, GENERATOR, str(published), staged, "--root=crate::types"],
                       check=True, capture_output=True, text=True)
        _sync(staged, types)
    return copy


def make_workspace(root, tempo_sim, extra_args=()):
    """A host crate holding the fixture's types, and a snippet crate depending on it.

    `tempo-sim` arrives as a symlink beside the host crate, so the layout under test is the one
    `Packaged/API/Rust` arranges, and both crates name it by the same `../tempo-sim` -- one path, so
    cargo sees one tempo-sim rather than two whose types do not unify.
    """
    os.symlink(tempo_sim, str(root / "tempo-sim"))
    host = root / CRATE
    subprocess.run(
        [sys.executable, GENERATOR, FIXTURE, str(host / "src" / "types"), "--root=crate::types"]
        + list(extra_args),
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
    """The fixture's types in a host crate, with nothing published to re-export."""
    if cargo_executable() is None:
        pytest.skip("cargo is not installed")
    return make_workspace(tmp_path_factory.mktemp("rust"), tempo_sim_copy("plain"))


def published_fixture(directory):
    """The fixture's published subset, altered as a different release would be.

    Widget hashes differently, so a project has to define its own -- and so must everything
    deriving from it. MeshThing and Object match, and are re-exported.
    """
    import json

    sys.path.insert(0, PYTHON_DIR)
    try:
        import subset_schema
    finally:
        sys.path.remove(PYTHON_DIR)
    with open(FIXTURE) as f:
        schema = json.load(f)
    published, _ = subset_schema.subset(schema, ("/Script/CoreUObject", "/Script/Engine"))
    for cls in published["classes"]:
        if cls["path"] == "/Script/Engine.Widget":
            cls["content_hash"] = "A-DIFFERENT-WIDGET"
    path = directory / "published.json"
    path.write_text(json.dumps(published))
    return path


@pytest.fixture(scope="module")
def reexporting(tmp_path_factory):
    """The fixture's types against published ones they partly share."""
    if cargo_executable() is None:
        pytest.skip("cargo is not installed")
    root = tmp_path_factory.mktemp("rust-reexporting")
    published = published_fixture(root)
    tempo_sim = tempo_sim_copy("published", published, root)
    return make_workspace(root, tempo_sim, ["--published={}".format(published)])


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
    for name in ("Cargo.toml", "src/lib.rs", "src/types/mod.rs", "src/types/enums.rs",
                 "src/types/structs.rs", "src/types/hierarchy.rs", "src/types/classes.rs",
                 "src/types/classes/engine.rs"):
        assert (crate / name).exists(), "{} was not generated".format(name)


def test_the_host_crate_declares_the_classes_behind_a_default_feature(workspace):
    """A crate with classes and no protos: no proto module, no proto dependencies, `types` on."""
    crate = workspace / CRATE
    lib = (crate / "src" / "lib.rs").read_text()
    assert '#[cfg(feature = "types")]\npub mod types;' in lib
    assert "pub mod proto;" not in lib
    manifest = (crate / "Cargo.toml").read_text()
    assert 'default = ["types"]' in manifest
    assert "tonic" not in manifest


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
    assert "fn name(" not in (workspace / CRATE / "src" / "types" / "classes" / "engine.rs").read_text(), \
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


def test_the_asset_catalog_compiles(workspace):
    assert check(workspace, """
    use fixture_project::types::assets::game::widgets::W_Main;
    use tempo_sim::unreal::AssetRef;
    widget.set_skin(&W_Main)?;
    // A MeshThing asset is an Object asset, through the same Deref chain as the classes.
    widget.set_any_asset(&W_Main)?;
    let _: bool = widget.reskin(&fixture_project::types::assets::game::maps::showroom::Lighting)?;
    let _: AssetRef<MeshThing> = widget.skin()?;
    // A map and the folder beside it, which Rust keeps in separate namespaces.
    let _ = &fixture_project::types::assets::game::maps::Showroom;
    let _ = &fixture_project::types::assets::engine::basic::_9Grid;
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
    use fixture_project::types::enums::EWidgetComparison;
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
    let _: bool = widget.set_frame(&fixture_project::types::assets::game::widgets::W_Main)?;
    let _ = widget.frame()?;
""") == []


@pytest.mark.parametrize("case,body", [
    ("an asset that was renamed",
     "widget.set_skin(&fixture_project::types::assets::game::widgets::W_Mian)?;"),
    ("an asset of the wrong class",
     "widget.set_skin(&fixture_project::types::assets::game::widgets::trims::T_Gold)?;"),
    ("a bare path", 'widget.set_skin("/Game/Widgets/W_Main")?;'),
    ("two assets that collide, both left out",
     "let _ = &fixture_project::types::assets::game::widgets::Dup_One;"),
])
def test_asset_drift_does_not_compile(workspace, case, body):
    assert check(workspace, "    " + body), "{} must not compile".format(case)


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
    engine = (workspace / CRATE / "src" / "types" / "classes" / "engine.rs").read_text()
    assert 'set_via("SetScaleFactor", "InScale"' in engine, \
        "the setter and its argument both have to be named: {}".format(
            [line.strip() for line in engine.splitlines() if "set_via" in line])


def test_a_property_whose_setter_cannot_be_named_is_read_only(workspace):
    """Writing it directly would skip the setter, which is what the accessor path prevents."""
    engine = (workspace / CRATE / "src" / "types" / "classes" / "engine.rs").read_text()
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
     "let _ = fixture_project::types::structs::FWidgetLimits::default();"),
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


# -- re-exporting the published types (section 16.5) ---------------------------------------------

def test_a_shared_class_is_the_published_type(reexporting):
    """A helper written against tempo-sim's type takes the project's: they are one type."""
    assert check(reexporting, """
    fn published_mesh(_m: &tempo_sim::types::classes::engine::MeshThing) {}
    published_mesh(&mesh);
    let _object: &tempo_sim::types::classes::core_u_object::Object = &mesh;
""") == []


def test_a_class_the_release_describes_differently_is_the_projects_own(reexporting):
    assert check(reexporting, """
    fn published_widget(_w: &tempo_sim::types::classes::engine::Widget) {}
    published_widget(&widget);
""") != []


def test_the_project_table_brings_in_the_published_one(reexporting):
    """MeshThing's superclass is in the published table only; registering the project's finds it."""
    stdout, stderr, status = run(reexporting, """
    tempo_sim::unreal::register_classes(<Widget as Class>::hierarchy());
    let as_mesh = ObjectRef::actor("m").with_class(MeshThing::PATH);
    println!("{}", as_mesh.is_a(Object::PATH));
""")
    assert status == 0, stderr
    assert stdout.strip() == "true"


def test_a_re_exported_entry_is_left_out_of_the_project_table(reexporting):
    hierarchy = (reexporting / CRATE / "src" / "types" / "hierarchy.rs").read_text()
    # Its superclass entry, not the name: an asset's class is in the table either way.
    assert '("/Script/Engine.MeshThing", "/Script/CoreUObject.Object")' not in hierarchy
    assert '("/Script/Engine.Widget", "/Script/CoreUObject.Object")' in hierarchy
    assert "fn depends_on" in hierarchy
    manifest = (reexporting / CRATE / "Cargo.toml").read_text()
    assert 'types = ["tempo-sim/types"]' in manifest

