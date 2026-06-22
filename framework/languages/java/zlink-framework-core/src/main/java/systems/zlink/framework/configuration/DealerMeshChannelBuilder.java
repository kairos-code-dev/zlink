package systems.zlink.framework.configuration;

public interface DealerMeshChannelBuilder {
    DealerMeshChannelBuilder enableClient();

    DealerMeshChannelBuilder enableClient(String endpoint);

    DealerMeshChannelBuilder setDefaultRequestTimeout(java.time.Duration timeout);

    DealerMeshChannelBuilder addHandlerGroup(String groupName);
}
