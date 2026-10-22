# Copyright Tempo Simulation, LLC. All Rights Reserved
"""A tour of the typed classes: the same world API, with the strings removed.

Every attribute in this file is a declared member of a generated class, so a type checker reads it
against the build those classes came from. That is the point of the typed classes -- a property that
was renamed or removed in the sim is an error here before anything runs, instead of a NOT_FOUND
somewhere in the middle of a scenario.

Run it against a packaged TempoSample (see docs/clients/typed-classes.md for the setup):

    pip install Packaged/API/Python/tempo-sim/*.whl \
                Packaged/API/Python/tempo-sample-classes/*.whl
    python Plugins/Tempo/ExampleClients/Python/TypedClasses.py

The imports below are TempoSample's own classes. In another project they are that project's --
`<project>_classes` is generated from whatever that build happens to contain -- but everything after
the imports is the same API.
"""

import argparse

import tempo_sim
from tempo_sim import world
from tempo_sim.unreal import Transform, Vector

# Generated: one module per Unreal package. Engine classes and the project's Blueprints arrive the
# same way, and the import path mirrors the content path -- /Game/StreetSweeper/Blueprint here.
from tempo_sample_classes.Engine import Pawn
from tempo_sample_classes.Game.StreetSweeper.Blueprint import BP_StreetSweeper_C

SPAWN_LOCATION = Vector(X=0.0, Y=0.0, Z=500.0)


def heading(text: str) -> None:
    print("\n--- {} ---".format(text))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ip", default="localhost")
    parser.add_argument("--port", type=int, default=10001)
    parser.add_argument("--socket", default=None,
                        help="connect over a Unix domain socket instead of TCP")
    parser.add_argument("--check-schema", action="store_true",
                        help="verify the installed classes describe this build (takes a minute)")
    args = parser.parse_args()

    if args.socket is not None:
        tempo_sim.set_socket(args.socket)
    else:
        tempo_sim.set_server(args.ip, args.port)

    if args.check_schema:
        # The sim answers by walking its whole reflection, which is why this is explicit rather
        # than automatic. Worth one call at the start of a long run, or from a test.
        heading("Checking that the classes match the sim")
        world.check_schema()
        print("the installed classes describe this build")

    # -- Spawning ------------------------------------------------------------------------------
    # The class is the argument, so the handle comes back typed: this is a BP_StreetSweeper_C, and
    # a checker knows it.
    heading("Spawning")
    sweeper = world.spawn_actor(BP_StreetSweeper_C, transform=Transform(Location=SPAWN_LOCATION))
    print("spawned {}".format(sweeper))

    # -- Properties ----------------------------------------------------------------------------
    # Attribute access is the API: reading asks the server, assigning tells it.
    heading("Properties")
    print("RotationRate was {}".format(sweeper.RotationRate))
    sweeper.RotationRate = 120.0
    print("RotationRate is now {}".format(sweeper.RotationRate))

    # -- Components ----------------------------------------------------------------------------
    # A component the class brings with it is typed as its own class, all the way down, so its
    # members are checked too.
    heading("Components")
    print("Body is a {}".format(type(sweeper.Body).__name__))
    print("Body.RelativeLocation = {}".format(sweeper.Body.RelativeLocation))
    movement = sweeper.KinematicUnicycleModelMovement
    print("steering factor {}".format(movement.SteeringToAngularVelocityFactor))

    # -- Functions -----------------------------------------------------------------------------
    # Assigning to a property writes that property and nothing else. Unreal recomputes a
    # component's world transform in UpdateComponentToWorld, which a property write does not call,
    # so moving something means calling a function.
    heading("Functions")
    target = Vector(X=SPAWN_LOCATION.X + 300.0, Y=SPAWN_LOCATION.Y, Z=SPAWN_LOCATION.Z)
    sweeper.K2_SetActorLocation(NewLocation=target, bSweep=False, bTeleport=True)
    print("moved to {}".format(sweeper.K2_GetActorLocation()))

    # -- One round trip instead of several -----------------------------------------------------
    # Each attribute access is a round trip, which is the price of looking like a field. When that
    # matters, batch the writes and snapshot the reads.
    heading("Batching and snapshots")
    with world.batch():
        sweeper.RotationRate = 45.0
        movement.SteeringToAngularVelocityFactor = 1.5
    print("two writes, one call")

    with sweeper.snapshot():
        print("RotationRate {} (read from the snapshot)".format(sweeper.RotationRate))

    # -- Watching ------------------------------------------------------------------------------
    # A server-side stream: one value per frame in which the property changed. Here the example
    # drives it, so that it is self-contained; usually the thing you watch is driven by the sim.
    heading("Watching")
    for index, value in enumerate(sweeper.watch("RotationRate")):
        print("  RotationRate -> {}".format(value))
        if index == 2:
            break
        sweeper.RotationRate = 45.0 + 15.0 * (index + 1)

    # -- Discovery by type ---------------------------------------------------------------------
    # Prefer this over a name: there is no unchecked string in it, and an actor's name in a
    # packaged build is not the label from the editor.
    heading("Discovery")
    pawns = world.get_actors(Pawn)
    print("{} pawn(s) in the world; our sweeper among them: {}".format(
        len(pawns), any(pawn.is_a(BP_StreetSweeper_C) for pawn in pawns)))

    # Each of these is an error from a type checker, before it ever runs. Uncomment one and run
    # `pyright` over this file to watch it get caught:
    #
    #   sweeper.RotatoinRate = 45.0              # misspelled -- no such member
    #   sweeper.Body.RelativeLocation = target    # read-only: BlueprintReadOnly in the engine
    #   sweeper.RotationRate = "fast"             # wrong type
    #   sweeper.Body.NoSuchThing                  # not a member of StaticMeshComponent

    heading("Cleaning up")
    sweeper.K2_DestroyActor()
    print("destroyed")


if __name__ == "__main__":
    main()
