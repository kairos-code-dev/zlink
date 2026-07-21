package systems.zlink.framework.monitoring;

public record ZLinkMeshClaimSnapshot(
    boolean applicationActive,
    long pendingApplicationWork,
    boolean infrastructureActive,
    long pendingInfrastructureWork) {
}
