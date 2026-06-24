package systems.zlink.framework.configuration;

public interface ZLinkSpotMeshBuilder extends ZLinkSpotNodeBuilder {
    ZLinkDiscoveryBuilder useDiscovery();

    ZLinkSpotMeshBuilder useRegistrySpotResolver();
}
