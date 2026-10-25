# Copyright Tempo Simulation, LLC. All Rights Reserved
"""Tests for the generated typed C++ class surface.

The same shape as test_rust_codegen.py, and for the same reason: what matters is whether the
compiler rejects what it has to reject, not whether the emitted text changed. So each case
generates from the same fixture schema and asserts what the host C++ compiler says about a snippet.
A snippet that should compile is only syntax-checked, which needs no linking; the few that have to
run -- the value encodings, the conversions, a batch, check_schema's comparison -- are built in a
CMake workspace kept under Saved/, so only the first run pays for compiling the runtime.

The runtime is the working tree's (TempoCore/Content/Python/cpp_api_template), compiled here rather
than taken from a built libtempo, which may be older. What does have to come from a build is what
the runtime compiles against: the protoc-generated headers of a C++ client built with
TEMPO_GEN_CPP_API=1, and the vendored gRPC. Skipped, not failed, when either is absent, or when
there is no C++ compiler.

    Scripts/TestPythonAPI.sh codegen
"""

import os
import shutil
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PYTHON_DIR = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Python")
GENERATOR = os.path.join(PYTHON_DIR, "gen_cpp_classes.py")
TEMPLATE = os.path.join(PYTHON_DIR, "cpp_api_template")
CPP_API = os.path.join(PLUGIN_ROOT, "TempoCore", "Content", "Cpp", "API")
GRPC = os.path.join(PLUGIN_ROOT, "TempoCore", "Source", "ThirdParty", "gRPC")
WORKSPACE = os.path.join(PLUGIN_ROOT, "Saved", "CppCodegen")
FIXTURE = os.path.join(HERE, "fixtures", "mini_schema.json")

pytestmark = pytest.mark.codegen

sys.path.insert(0, PYTHON_DIR)
from gen_naming import build_jobs, host_platform_name  # noqa: E402

DEFINES = ["GOOGLE_PROTOBUF_NO_RTTI=1", "GPR_FORBID_UNREACHABLE_CODE=1", "GRPC_ALLOW_EXCEPTIONS=0",
           "PROTOBUF_USE_DLLS", "ABSL_CONSUME_DLL"]

PREAMBLE = """#include <cmath>
#include <cstdio>

#include "tempo/conversion.h"
#include "tempo/world.h"
#include "tempo/reflected/core_u_object.h"
#include "tempo/reflected/engine.h"
#include "tempo/reflected/assets.h"

using namespace tempo;
using namespace tempo::unreal;
using namespace tempo::reflected::classes::engine;
using namespace tempo::reflected::structs;
using tempo::reflected::classes::core_u_object::Object;
using tempo::reflected::enums::EWidgetMode;

static void takes_mesh(const MeshThing&) {}

static tempo::Result<void> body() {
    Widget widget(ObjectRef::actor("a"));
    MeshThing mesh(ObjectRef::actor("b"));
    (void)widget; (void)mesh;
"""

EPILOGUE = """
    return {};
}

int main() { return body().ok() ? 0 : 1; }
"""


def compiler():
    for name in ("c++", "clang++", "g++"):
        found = shutil.which(name)
        if found:
            return found
    return None


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    """The fixture schema as C++, after checking there is something to compile it against."""
    if compiler() is None:
        pytest.skip("no C++ compiler on PATH")
    if not os.path.isdir(os.path.join(CPP_API, "include", "proto", "TempoWorld")):
        pytest.skip("the C++ client has not been built (a build with TEMPO_GEN_CPP_API=1 makes it)")
    if not os.path.isdir(os.path.join(GRPC, "Includes")):
        pytest.skip("the vendored gRPC is not here (Scripts/SyncDeps.sh fetches it)")
    out = tmp_path_factory.mktemp("cpp-generated")
    subprocess.run([sys.executable, GENERATOR, FIXTURE, str(out)], check=True,
                   capture_output=True, text=True)
    return out


def flags(generated):
    # The working tree's runtime headers first, ahead of whatever copy a build installed.
    return (["-std=c++20", "-fno-rtti", "-I" + str(generated / "include"),
             "-I" + os.path.join(TEMPLATE, "include"), "-I" + os.path.join(CPP_API, "include"),
             "-I" + os.path.join(CPP_API, "include", "proto"),
             "-isystem", os.path.join(GRPC, "Includes")]
            + ["-D" + define for define in DEFINES])


def check(generated, source, tmp_path):
    """Syntax-check one snippet body. Returns the compiler's errors about the snippet."""
    snippet = tmp_path / "snippet.cc"
    snippet.write_text(PREAMBLE + source + EPILOGUE)
    result = subprocess.run([compiler(), "-fsyntax-only"] + flags(generated) + [str(snippet)],
                            capture_output=True, text=True)
    errors = [line for line in result.stderr.splitlines()
              if "snippet.cc" in line and "error" in line]
    # A generated header that does not compile never gets as far as the snippet, which would
    # otherwise read as a snippet with no errors.
    assert errors or result.returncode == 0, result.stderr
    return errors


WORKSPACE_CMAKE = """cmake_minimum_required(VERSION 3.20)
project(cpp_codegen CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS "${TEMPLATE}/src/*.cc")
file(GLOB_RECURSE FIXTURE_SOURCES CONFIGURE_DEPENDS "${GENERATED}/src/*.cc")

add_library(runtime STATIC ${RUNTIME_SOURCES})
target_include_directories(runtime PUBLIC "${TEMPLATE}/include" "${API}/include" "${API}/include/proto")
target_include_directories(runtime SYSTEM PUBLIC "${GRPC}/Includes")
target_compile_definitions(runtime PUBLIC ${DEFINES})
target_compile_options(runtime PUBLIC -fno-rtti)

add_library(fixture STATIC ${FIXTURE_SOURCES})
target_include_directories(fixture PUBLIC "${GENERATED}/include")
target_link_libraries(fixture PUBLIC runtime)

# The protos and the service stubs come from the built libtempo; its own copy of the runtime is
# never pulled in, since the working tree's above defines everything it would.
find_library(TEMPOGRPC tempogrpc PATHS "${GRPC}/Libraries/${PLATFORM}" NO_DEFAULT_PATH REQUIRED)
add_executable(snippet snippet.cc)
target_link_libraries(snippet PRIVATE fixture runtime "${API}/lib/${PLATFORM}/libtempo.a" "${TEMPOGRPC}")
set_target_properties(snippet PROPERTIES BUILD_RPATH "${GRPC}/Libraries/${PLATFORM}")
"""


def run(generated, source):
    """Build one snippet body against the working tree's runtime and run it.

    Returns (stdout, stderr, exit status).
    """
    if shutil.which("cmake") is None:
        pytest.skip("cmake is not on PATH")
    archive = os.path.join(CPP_API, "lib", host_platform_name(), "libtempo.a")
    if not os.path.exists(archive):
        pytest.skip("libtempo has not been built for this platform")
    # A fixed path, so the runtime and the fixture are compiled once across runs; the fixture is
    # copied in, since the generated directory is a new temporary each session.
    os.makedirs(WORKSPACE, exist_ok=True)
    fixture_copy = os.path.join(WORKSPACE, "generated")
    sync(str(generated), fixture_copy)
    with open(os.path.join(WORKSPACE, "CMakeLists.txt"), "w") as f:
        f.write(WORKSPACE_CMAKE)
    with open(os.path.join(WORKSPACE, "snippet.cc"), "w") as f:
        f.write(PREAMBLE + source + EPILOGUE)
    build = os.path.join(WORKSPACE, "build")
    configure = subprocess.run(
        ["cmake", "-S", WORKSPACE, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
         "-DTEMPLATE=" + TEMPLATE, "-DGENERATED=" + fixture_copy, "-DAPI=" + CPP_API,
         "-DGRPC=" + GRPC, "-DPLATFORM=" + host_platform_name(), "-DDEFINES=" + ";".join(DEFINES)],
        capture_output=True, text=True)
    assert configure.returncode == 0, configure.stdout + configure.stderr
    built = subprocess.run(["cmake", "--build", build, "--parallel", str(build_jobs())],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout[-4000:] + built.stderr[-4000:]
    result = subprocess.run([os.path.join(build, "snippet")], capture_output=True, text=True)
    return result.stdout, result.stderr, result.returncode


def sync(source, destination):
    """Copy a tree, leaving files that did not change untouched so make does not rebuild them."""
    for root, _, names in os.walk(source):
        target_root = os.path.join(destination, os.path.relpath(root, source))
        os.makedirs(target_root, exist_ok=True)
        for name in names:
            src, dst = os.path.join(root, name), os.path.join(target_root, name)
            with open(src, "rb") as f:
                data = f.read()
            if os.path.exists(dst):
                with open(dst, "rb") as f:
                    if f.read() == data:
                        continue
            with open(dst, "wb") as f:
                f.write(data)


# -- the surface exists and is typed -------------------------------------------------------------

def test_generates_the_expected_files(generated):
    for name in ("include/tempo/reflected/enums.h", "include/tempo/reflected/structs.h",
                 "include/tempo/reflected/structs_fwd.h", "include/tempo/reflected/hierarchy.h",
                 "include/tempo/reflected/engine.h",
                 "include/tempo/reflected/classes/engine/Widget.h",
                 "include/tempo/reflected/assets/game.h", "src/hierarchy.cc", "src/enums.cc",
                 "src/structs.cc", "CMakeLists.txt"):
        assert (generated / name).exists(), "{} was not generated".format(name)


def test_every_generated_source_compiles(generated, tmp_path):
    """The definitions, which a snippet never sees: a class's .cc is only compiled into the archive."""
    for source in sorted((generated / "src").rglob("*.cc")):
        result = subprocess.run([compiler(), "-fsyntax-only"] + flags(generated) + [str(source)],
                                capture_output=True, text=True)
        assert result.returncode == 0, "{}:\n{}".format(source.name, result.stderr[-3000:])


def test_a_property_reads_and_writes(generated, tmp_path):
    assert check(generated, """
    Result<bool> enabled = widget.b_enabled();
    Result<void> written = widget.set_b_enabled(true);
    Result<FVector> offset = widget.offset();
""", tmp_path) == []


def test_an_inherited_property_is_inherited(generated, tmp_path):
    """Name is declared on Object, and Widget derives from it -- by C++'s own inheritance."""
    header = (generated / "include/tempo/reflected/classes/engine/Widget.h").read_text()
    assert " name() const" not in header, "Name must come from Object, not be copied into Widget"
    assert check(generated, """
    Result<std::string> name = widget.name();
""", tmp_path) == []


def test_a_derived_handle_is_a_base_one(generated, tmp_path):
    assert check(generated, """
    auto takes_object = [](const Object&) {};
    takes_object(widget);
    const Object& as_object = widget;
    (void)as_object;
""", tmp_path) == []


def test_a_component_is_typed_as_its_own_class(generated, tmp_path):
    assert check(generated, """
    Result<bool> visible = widget.mesh().visible();
""", tmp_path) == []


def test_a_reference_is_typed_as_the_class_it_points_at(generated, tmp_path):
    assert check(generated, """
    Result<MeshThing> target = widget.target();
    if (target) { Result<bool> visible = target.value().visible(); }
    Result<void> pointed = widget.set_target(mesh);
""", tmp_path) == []


def test_a_struct_property_is_addressed_a_member_at_a_time(generated, tmp_path):
    assert check(generated, """
    Result<double> scale = widget.settings().scale();
    Result<void> inner = widget.settings().inner().set_offset(FVector{});
""", tmp_path) == []


def test_struct_values_compile(generated, tmp_path):
    assert check(generated, """
    FWidgetSettings settings;
    settings.scale = 2.0;
    settings.inner = FWidgetTrim{FVector{}};
    Result<void> set = widget.set_settings(settings);
    Result<FWidgetSettings> back = widget.configure(settings);
    if (back) { std::optional<std::string> label = back.value().label; }
    Result<FWidgetSettings> copy = widget.settings().load();
    Result<void> stored = widget.settings().store(settings);
    Result<void> history = widget.set_history({FWidgetSettings{}});
    Result<std::vector<FWidgetSettings>> read = widget.history();
    Result<double> floor = widget.limits().floor();
""", tmp_path) == []


def test_out_parameters_unpack_by_name(generated, tmp_path):
    assert check(generated, """
    auto measured = widget.measure();
    if (measured) {
        auto [fits, settings] = measured.value();
        bool f = fits;
        FWidgetSettings s = settings;
    }
""", tmp_path) == []


def test_a_deferred_read_is_typed_by_what_it_reads(generated, tmp_path):
    assert check(generated, """
    Result<void> batched = world::batch([&]() -> Result<void> {
        if (auto r = widget.set_b_enabled(true); !r) return r;
        Result<world::Deferred<bool>> enabled = world::defer([&] { return widget.b_enabled(); });
        Result<world::Deferred<bool>> resized = world::defer([&] { return widget.resize(4); });
        return {};
    });
""", tmp_path) == []
    assert check(generated, """
    Result<world::Deferred<std::string>> wrong = world::defer([&] { return widget.b_enabled(); });
""", tmp_path), "a deferred read must keep the type of what it reads"


def test_the_asset_catalog_compiles(generated, tmp_path):
    assert check(generated, """
    namespace assets = tempo::reflected::assets;
    Result<void> skin = widget.set_skin(assets::game::widgets::W_Main);
    // A MeshThing asset is an Object asset, by the classes' own inheritance.
    Result<void> any = widget.set_any_asset(assets::game::widgets::W_Main);
    Result<bool> reskinned = widget.reskin(assets::game::maps::showroom::Lighting);
    Result<AssetRef<MeshThing>> read = widget.skin();
    // A map and the folder beside it, which do not collide: the folder's namespace is snake_case.
    const auto& showroom = assets::game::maps::Showroom;
    const auto& grid = assets::engine::basic::_9Grid;
    // The escape hatch: a path from data, typed by what it is written as, visibly so.
    Result<void> from_data = widget.set_skin(AssetRef<MeshThing>("/Game/FromAConfigFile"));
""", tmp_path) == []


def test_a_subsystem_is_reached_by_its_class(generated, tmp_path):
    assert check(generated, """
    Result<Widget> subsystem = world::get_subsystem<Widget>();
    if (subsystem) { Result<bool> enabled = subsystem.value().b_enabled(); }
""", tmp_path) == []


def test_discovery_and_spawning_are_typed(generated, tmp_path):
    assert check(generated, """
    Result<std::vector<Widget>> widgets = world::get_actors<Widget>();
    Result<Widget> one = world::get_actor<Widget>("Widget_1");
    world::Transform at;
    at.mutable_location()->set_z(1.0);   // SI, as tempo_world takes it: a meter up
    Result<Widget> spawned = world::spawn_actor<Widget>(at);
    Result<Widget> deferred = world::spawn_actor_deferred<Widget>(
        std::nullopt, nullptr, [](const Widget& w) { return w.set_b_enabled(false); });
    bool is_object = widget.is_a<Object>();
""", tmp_path) == []


def test_the_world_mirror_takes_handles_and_si(generated, tmp_path):
    """world is tempo_world given handles: the same functions, SI arguments, typed results where
    something is made."""
    assert check(generated, """
    Result<void> destroyed = world::destroy_actor(widget);
    Result<MeshThing> light = world::add_component<MeshThing>(widget, "Light", &mesh);
    Result<std::vector<MeshThing>> meshes = world::get_components<MeshThing>(widget);
    Result<void> moved = world::set_actor_location(widget, world::Vector(), &mesh);
    Result<void> turned = world::set_component_rotation(mesh, world::Rotation(), true);
    Result<TempoWorld::GetTransformResponse> pose =
        world::get_component_transform(mesh, TempoWorld::CF_ACTOR, nullptr, "");
    Result<TempoWorld::ActorState> state = world::get_current_actor_state(widget);
    Result<TempoWorld::RaycastResponse> hit =
        world::raycast(world::Vector(), world::Vector(), TempoWorld::CC_VISIBILITY, {&widget, &mesh});
    auto stream = world::stream_actor_state(widget);
""", tmp_path) == []
    assert check(generated, """
    auto r = world::set_actor_location(widget, FVector{});
""", tmp_path), "world takes SI types, as tempo_world does, not Unreal's"
    assert check(generated, """
    auto r = world::add_component<FWidgetSettings>(widget);
""", tmp_path), "only a class can be added as a component"


def test_a_setter_names_the_argument_it_sends(generated):
    """A BlueprintSetter write has to carry the setter's own parameter name: CallFunction refuses
    an unnamed argument, so every write through a setter failed until the generator resolved it."""
    definitions = "".join(p.read_text() for p in (generated / "src" / "classes").glob("engine_*.cc"))
    assert 'set_via("SetScaleFactor", "InScale"' in definitions


def test_a_property_whose_setter_cannot_be_named_is_read_only(generated, tmp_path):
    header = (generated / "include/tempo/reflected/classes/engine/Widget.h").read_text()
    assert " untouchable() const" in header, "it should still be readable"
    assert "set_untouchable" not in header, "it must not be writable"


def test_a_read_only_propertys_set_function_is_kept(generated, tmp_path):
    assert check(generated, """
    Result<bool> set = widget.set_frame(tempo::reflected::assets::game::widgets::W_Main);
    auto frame = widget.frame();
""", tmp_path) == []


def test_every_enumerator_has_a_member(generated, tmp_path):
    """Symbol-only enumerators are numbered, and names that mangle alike are all kept."""
    header = (generated / "include/tempo/reflected/enums.h").read_text()
    for member in ("Enumerator0", "Enumerator1", "Enumerator1_2", "Less_Than", "Less_Than_4",
                   "Greater"):
        assert "\t{},".format(member) in header, member


# -- drift must not compile ----------------------------------------------------------------------

@pytest.mark.parametrize("case,body", [
    ("a misspelled property", "auto r = widget.b_enabeld();"),
    ("a write to a read-only property", "auto r = widget.set_offset(FVector{});"),
    ("a wrong value type", 'auto r = widget.set_b_enabled(std::string("yes"));'),
    ("a property of an unrelated class", "auto r = widget.visible();"),
    ("an unrelated class where this one is wanted", "takes_mesh(widget);"),
    ("a member the pointee does not have", "auto r = widget.target().value().nope();"),
    ("an enumerator the enum does not have", "auto r = widget.set_mode(EWidgetMode::Nope);"),
    ("watching a property the class lacks", "auto r = widget.watch_visible();"),
    ("a function the class does not have", "auto r = widget.vanish();"),
    ("a function called with the wrong arity", "auto r = widget.resize();"),
    ("discovery by a type that is not a class", "auto r = world::get_actors<int>();"),
    ("discovery by a struct value", "auto r = world::get_actors<FWidgetSettings>();"),
    ("a struct member the struct lacks", "auto r = widget.settings().nope();"),
    ("a struct value member the struct lacks", "FWidgetSettings s; s.scal = 1.0;"),
    ("a struct value member of the wrong type", 'FWidgetSettings s; s.scale = std::string("big");'),
    ("the wrong struct as an argument", "auto r = widget.configure(FWidgetTrim{});"),
    ("a value type for a struct that cannot travel", "FWidgetLimits limits;"),
    ("loading a struct that cannot travel", "auto r = widget.limits().load();"),
    ("a function returning a struct that cannot travel", "auto r = widget.get_limits();"),
    ("a function taking a struct that cannot travel", "auto r = widget.apply_limits();"),
    ("a container nothing can read", "auto r = widget.waypoints();"),
    ("an asset that was renamed",
     "auto r = widget.set_skin(tempo::reflected::assets::game::widgets::W_Mian);"),
    ("an asset of the wrong class",
     "auto r = widget.set_skin(tempo::reflected::assets::game::widgets::trims::T_Gold);"),
    ("a bare path", 'auto r = widget.set_skin("/Game/Widgets/W_Main");'),
    ("two assets that collide, both left out",
     "auto& r = tempo::reflected::assets::game::widgets::Dup_One;"),
    ("a color converted to SI", "auto r = conversion::ue_to_si(FColor{});"),
    ("an SI value converted the wrong way",
     "auto r = conversion::ue_to_si(TempoCore::Vector{});"),
    ("the old prefixed name", "tempo::unreal::UnrealVector v;"),
    ("the old unprefixed name", "tempo::unreal::Vector v;"),
    ("the string API without naming it", 'auto r = widget.get<int>("WhateverThisIs");'),
])
def test_drift_does_not_compile(generated, tmp_path, case, body):
    """Each of these is what a release is expected to break, and must break the build."""
    assert check(generated, "    " + body, tmp_path), "{} must not compile".format(case)


def test_the_escape_hatch_is_explicit(generated, tmp_path):
    assert check(generated, """
    Result<std::int32_t> anything = widget.object().get<std::int32_t>("WhateverThisIs");
""", tmp_path) == []


# -- what has to run -----------------------------------------------------------------------------

def test_values_conversions_and_batches_behave(generated):
    """The parts of the runtime that need no sim, run: the value encodings, the conversions, a
    batch that never sends, the hierarchy, and check_schema's comparison."""
    stdout, stderr, status = run(generated, r"""
    auto expect = [](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok" : "FAILED", what); };

    // A struct value round-trips, and an unset member is not sent: a write is a merge.
    FWidgetSettings settings;
    settings.scale = 2.5;
    settings.label = "a";
    TempoWorld::Value wire = ValueTraits<FWidgetSettings>::to(settings);
    expect(wire.struct_value().struct_name() == "FWidgetSettings", "struct named");
    expect(wire.struct_value().fields_size() == 2, "unset members not sent");
    auto back = ValueTraits<FWidgetSettings>::from(wire);
    expect(back.ok() && back.value() == settings, "struct round trip");
    wire.mutable_struct_value()->set_struct_name("FSomethingElse");
    expect(!ValueTraits<FWidgetSettings>::from(wire).ok(), "a different struct is refused");

    // An enum travels as its authored name.
    TempoWorld::Value mode = ValueTraits<EWidgetMode>::to(EWidgetMode::Advanced);
    expect(mode.enum_value() == "Advanced", "enum by name");
    expect(ValueTraits<EWidgetMode>::from(mode).value() == EWidgetMode::Advanced, "enum round trip");
    using tempo::reflected::enums::EWidgetComparison;
    expect(EnumTraits<EWidgetComparison>::name_of(EWidgetComparison::Enumerator0) == "==",
           "a symbol-only enumerator keeps its authored name");

    // The server's conversion rule: cm -> m with Y negated, degrees -> radians with pitch and yaw
    // negated, and a direction's handedness alone.
    TempoCore::Vector si = conversion::ue_to_si(FVector{100, 50, -25});
    expect(si.x() == 1 && si.y() == -0.5 && si.z() == -0.25, "a length converts");
    expect(conversion::ue_to_si(250.0) == 2.5 && conversion::si_to_ue(2.5) == 250.0, "a bare number is a length");
    TempoCore::Rotation r = conversion::ue_to_si(FRotator{90, 45, 180});   // pitch, yaw, roll
    expect(std::abs(r.r() - 3.141592653589793) < 1e-12 && std::abs(r.p() + 1.5707963267948966) < 1e-12 &&
           std::abs(r.y() + 0.7853981633974483) < 1e-12, "a rotation converts");
    expect(conversion::ue_direction_to_si(FVector{0, 1, 0}).y() == -1, "a direction is not scaled");
    // Scale 1: TempoCore::Transform carries none, so a round trip comes back with 1.
    FTransform pose{conversion::quat_of(FRotator{5, 6, 4}), FVector{1, 2, 3}};
    FTransform round = conversion::si_to_ue(conversion::ue_to_si(pose));
    FRotator turned = conversion::rotator_of(round.rotation);
    expect(std::abs(round.translation.y - 2) < 1e-9 && std::abs(turned.pitch - 5) < 1e-9 &&
           std::abs(turned.yaw - 6) < 1e-9 && std::abs(turned.roll - 4) < 1e-9 && round.scale3d.x == 1,
           "a transform round-trips");
    expect(conversion::si_to_ue(TempoCore::Transform()) == FTransform{}, "unset is zero");
    FQuat quarter = conversion::quat_of(FRotator{0, 90, 0});
    expect(std::abs(quarter.z - std::sqrt(0.5)) < 1e-12 && std::abs(quarter.w - std::sqrt(0.5)) < 1e-12,
           "Unreal's FRotator::Quaternion");
    FRotator pole = conversion::rotator_of(conversion::quat_of(FRotator{90, 30, 0}));
    expect(std::abs(pole.pitch - 90) < 1e-9 && std::abs(pole.yaw - 30) < 1e-9 && pole.roll == 0,
           "Unreal's FQuat::Rotator, at the pole");

    // The value types travel in Unreal's own shapes.
    TempoWorld::Value rotator = ValueTraits<FRotator>::to(FRotator{10, 20, 30});
    expect(rotator.rotator_value().r() == 30 && rotator.rotator_value().p() == 10 &&
           rotator.rotator_value().y() == 20, "r/p/y are roll, pitch and yaw");
    FTransform scaled{FQuat{0, 0, 0.5, 0.5}, FVector{1, 2, 3}, FVector{2, 2, 2}};
    expect(ValueTraits<FTransform>::from(ValueTraits<FTransform>::to(scaled)).value() == scaled,
           "a transform keeps its scale");
    expect(ValueTraits<FColor>::from(ValueTraits<FColor>::to(FColor{1, 2, 3, 4})).value() == FColor{1, 2, 3, 4},
           "a color keeps its alpha");
    FLinearColor linear{0.25f, 0.5f, 0.75f, 0.125f};
    expect(ValueTraits<FLinearColor>::from(ValueTraits<FLinearColor>::to(linear)).value() == linear,
           "a linear color is not squeezed into 8 bits");
    expect(FTransform{}.scale3d == FVector{1, 1, 1} && FColor{}.a == 255, "Unreal's defaults");

    // A batch abandoned by its body sends nothing, which is what lets this run without a sim; a
    // deferred read inside it waits rather than resolving.
    bool waited = false;
    Result<void> abandoned = world::batch([&]() -> Result<void> {
        if (auto w = widget.set_b_enabled(true); !w) return w;
        auto deferred = world::defer([&] { return widget.b_enabled(); });
        waited = deferred.ok() && !deferred.value().is_done();
        auto both = world::defer([&]() -> Result<bool> {
            (void)widget.b_enabled();
            return widget.b_enabled();
        });
        expect(!both.ok(), "a deferred lambda makes exactly one read");
        return TempoError::protocol("abandoned");
    });
    expect(!abandoned.ok() && waited, "a deferred read is queued, not sent");
    auto ready = world::defer([] { return Result<int>(7); });
    expect(ready.ok() && ready.value().is_done() && ready.value().take().value() == 7,
           "outside a batch a deferred value is ready at once");

    // The hierarchy is compiled in, so is_a needs no sim.
    note_origin<Widget>();
    Widget typed(ObjectRef::actor("w").with_class(std::string(Widget::kPath)));
    expect(typed.is_a<Object>() && !typed.is_a<MeshThing>(), "is_a walks the hierarchy");

    // check_schema's comparison, against an answer built here.
    TempoWorld::DescribeClassesResponse reported;
    for (auto [path, hash] : tempo::reflected::hierarchy().hashes(EntryKind::Class)) {
        auto* entry = reported.add_classes();
        entry->set_class_path(std::string(path));
        entry->set_content_hash(std::string(hash));
    }
    for (auto [name, hash] : tempo::reflected::hierarchy().hashes(EntryKind::Struct)) {
        auto* entry = reported.add_structs();
        entry->set_name(std::string(name));
        entry->set_content_hash(std::string(hash));
    }
    for (auto [name, hash] : tempo::reflected::hierarchy().hashes(EntryKind::Enum)) {
        auto* entry = reported.add_enums();
        entry->set_name(std::string(name));
        entry->set_content_hash(std::string(hash));
    }
    std::vector<std::pair<std::string, std::string>> assets;
    for (auto [path, cls] : tempo::reflected::hierarchy().hashes(EntryKind::Asset)) {
        assets.emplace_back(std::string(path), std::string(cls));
    }
    expect(world::detail::compare(reported, assets).ok(), "matching hashes pass");
    reported.mutable_classes(0)->set_content_hash("SOMETHING-ELSE");
    auto changed = world::detail::compare(reported, assets);
    expect(!changed.ok() && changed.error().message().find("1 changed") != std::string::npos,
           "a changed class is named");
    reported.mutable_classes()->RemoveLast();
    auto absent = world::detail::compare(reported, assets);
    expect(!absent.ok() && absent.error().message().find("not in this sim") != std::string::npos,
           "a class the sim lacks is its own finding");
""")
    assert status == 0, stdout + stderr
    failures = [line for line in stdout.splitlines() if not line.startswith("ok ")]
    assert not failures, stdout
