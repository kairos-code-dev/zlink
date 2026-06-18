package systems.zlink.samples.gamequest.server.configuration;

import systems.zlink.contracts.core.RoutingId;

/** Resolved endpoints and owner index for a single QuestMission instance. */
public record QuestMissionInstanceTopology(
    String missionName,
    String spotEndpoint,
    String spotRouterEndpoint,
    RoutingId spotRid,
    int ownerIndex) {
}
