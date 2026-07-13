package systems.zlink.framework.configuration;

public interface ZLinkSpotMeshBuilder extends ZLinkSpotNodeBuilder {
    ZLinkSpotMeshBuilder useDrainPolicy(
        systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy policy);
}
