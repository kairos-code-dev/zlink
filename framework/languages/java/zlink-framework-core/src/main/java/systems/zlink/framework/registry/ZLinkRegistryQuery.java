package systems.zlink.framework.registry;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkRegistryQuery {
    CompletionStage<ZLinkRegistryStatus> statusAsync();

    CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter filter);

    CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter);
}
