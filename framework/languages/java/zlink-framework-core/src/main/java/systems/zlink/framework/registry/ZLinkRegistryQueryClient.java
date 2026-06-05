package systems.zlink.framework.registry;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkRegistryQueryClient extends AutoCloseable {
    default CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync() {
        return topologyAsync(ZLinkRegistryTopologyFilter.all());
    }

    CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter);

    @Override
    void close();
}
