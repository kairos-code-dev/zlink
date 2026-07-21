package systems.zlink.framework.monitoring;

public record ZLinkMeshForceStopped(
    ZLinkDrainForceReason reason) implements ZLinkMeshDrainResult {
}
