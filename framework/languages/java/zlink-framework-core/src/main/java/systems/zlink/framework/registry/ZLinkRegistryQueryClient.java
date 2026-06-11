package systems.zlink.framework.registry;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkRegistryQueryClient extends AutoCloseable {
    default CompletionStage<List<ZLinkRegistryTopologyEntry>> topology() {
        return topology(ZLinkRegistryTopologyFilter.all());
    }

    CompletionStage<List<ZLinkRegistryTopologyEntry>> topology(
        ZLinkRegistryTopologyFilter filter);

    @Override
    void close();
}
