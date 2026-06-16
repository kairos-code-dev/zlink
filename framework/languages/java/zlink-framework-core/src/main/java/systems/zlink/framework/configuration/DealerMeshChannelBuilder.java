package systems.zlink.framework.configuration;

public interface DealerMeshChannelBuilder {
    DealerMeshChannelBuilder enableClient();

    DealerMeshChannelBuilder enableClient(String endpoint);

    DealerMeshChannelBuilder addHandlerGroup(String groupName);
}
