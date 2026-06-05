package systems.zlink.framework.registry;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkRegistryQuery {
    CompletionStage<ZLinkRegistryStatus> statusAsync();

    default CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummaryAsync() {
        return serviceSummaryAsync(ZLinkRegistryServiceSummaryFilter.all());
    }

    CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter filter);

    default CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync() {
        return topologyAsync(ZLinkRegistryTopologyFilter.all());
    }

    CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter);

    CompletionStage<List<ZLinkMemberPeerEntry>> memberPeersAsync(String channelName);
}
