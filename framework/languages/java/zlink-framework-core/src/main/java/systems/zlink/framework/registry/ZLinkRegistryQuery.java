package systems.zlink.framework.registry;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkRegistryQuery {
    CompletionStage<ZLinkRegistryStatus> status();

    default CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummary() {
        return serviceSummary(ZLinkRegistryServiceSummaryFilter.all());
    }

    CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummary(
        ZLinkRegistryServiceSummaryFilter filter);

    default CompletionStage<List<ZLinkRegistryTopologyEntry>> topology() {
        return topology(ZLinkRegistryTopologyFilter.all());
    }

    CompletionStage<List<ZLinkRegistryTopologyEntry>> topology(
        ZLinkRegistryTopologyFilter filter);

    CompletionStage<List<ZLinkMemberPeerEntry>> memberPeers(String channelName);
}
