# TempoAgents

TempoAgents supports simulating large numbers of vehicle and crowd agents, with complex
intersection rules and dynamic interactions. It uses
[MassEntity](https://dev.epicgames.com/documentation/en-us/unreal-engine/mass-entity-in-unreal-engine)
to simulate very large numbers of agents simultaneously.

!!! note "Content is on the way"

    Tempo does not yet ship the content needed to do the agents system justice. We have a plan to
    add it — check back soon. In the meantime, the map query service below is fully usable
    against any ZoneGraph lane graph you build.

## Map query service

If your simulator includes a lane graph built with Unreal's ZoneGraph plugin, the map query
service can get or stream the lane graph — including the connectivity of lanes and the
accessibility of connected lanes as determined by traffic controls.

This is the ground truth an autonomy stack needs about the road network: where the lanes are,
which connect to which, and which of those connections is currently permitted.

| RPC | What it gives you |
|---|---|
| `get_lanes` | The lane graph — lane geometry and connectivity. |
| `get_zones` | The zones the lanes belong to. |
| `get_lane_accessibility` | Whether each connection out of a lane is currently accessible, as determined by traffic controls. |
| `stream_lane_accessibility` | The same, continuously — so a client sees signals change. |

`TempoAgentsEditor` additionally exposes `run_tempo_zone_graph_builder_pipeline`, which rebuilds
the ZoneGraph from the Editor over the API — useful in a content pipeline.

[:octicons-arrow-right-24: `TempoAgents` RPCs](../reference/api/tempo-agents.md)

## Settings

`Project Settings → Plugins → Tempo Agents` exposes the road configuration used when interpreting
intersections. See the [settings reference](../reference/settings.md#tempo-agents).
