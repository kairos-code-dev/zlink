package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.registry.ZLinkMemberPeerEntry;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;

final class RegistryContractTest {
    @Test
    void registryQueryUsesTypedSnapshotContracts() throws Exception {
        Method status = ZLinkRegistryQuery.class.getMethod("statusAsync");
        Method serviceSummary = ZLinkRegistryQuery.class.getMethod(
            "serviceSummaryAsync",
            ZLinkRegistryServiceSummaryFilter.class);
        Method serviceSummaryAll = ZLinkRegistryQuery.class.getMethod(
            "serviceSummaryAsync");
        Method topology = ZLinkRegistryQuery.class.getMethod(
            "topologyAsync",
            ZLinkRegistryTopologyFilter.class);
        Method topologyAll = ZLinkRegistryQuery.class.getMethod(
            "topologyAsync");
        Method memberPeers = ZLinkRegistryQuery.class.getMethod(
            "memberPeersAsync",
            String.class);

        assertEquals(CompletionStage.class, status.getReturnType());
        assertEquals(CompletionStage.class, serviceSummary.getReturnType());
        assertEquals(CompletionStage.class, serviceSummaryAll.getReturnType());
        assertEquals(CompletionStage.class, topology.getReturnType());
        assertEquals(CompletionStage.class, topologyAll.getReturnType());
        assertEquals(CompletionStage.class, memberPeers.getReturnType());
    }

    @Test
    void registryStatusCarriesDotnetStatusSnapshotFields() {
        assertEquals(
            List.of(
                "registryId",
                "bindEndpoint",
                "state",
                "topologyEntryCount",
                "peerRegistryCount",
                "connectedPeerRegistryCount",
                "listSeq",
                "lastError",
                "lastChangedMs"),
            java.util.Arrays.stream(ZLinkRegistryStatus.class.getRecordComponents())
                .map(java.lang.reflect.RecordComponent::getName)
                .toList());
    }

    @Test
    void registryServiceSummaryCarriesDotnetSummaryFields() {
        assertEquals(
            List.of(
                "autoConnectType",
                "serviceRole",
                "channelName",
                "totalCount",
                "connectingCount",
                "readyCount",
                "errorCount",
                "stoppedCount",
                "lastReportedMs"),
            java.util.Arrays.stream(ZLinkRegistryServiceSummaryEntry.class.getRecordComponents())
                .map(java.lang.reflect.RecordComponent::getName)
                .toList());
    }

    @Test
    void registryTopologyCarriesDotnetTopologyFields() {
        assertEquals(
            List.of(
                "autoConnectType",
                "routingId",
                "serviceKind",
                "serviceRole",
                "channelName",
                "endpoint",
                "source",
                "state",
                "desiredCount",
                "readyCount",
                "errorCode",
                "lastReportedMs",
                "spotKind"),
            java.util.Arrays.stream(ZLinkRegistryTopologyEntry.class.getRecordComponents())
                .map(java.lang.reflect.RecordComponent::getName)
                .toList());
    }

    @Test
    void registryMemberPeerCarriesDotnetMemberPeerFields() {
        assertEquals(
            List.of(
                "autoConnectType",
                "serviceRole",
                "channelName",
                "endpoint",
                "routingId",
                "value",
                "weight"),
            java.util.Arrays.stream(ZLinkMemberPeerEntry.class.getRecordComponents())
                .map(java.lang.reflect.RecordComponent::getName)
                .toList());
    }

    @Test
    void registryQueryClientOnlyExposesRemoteTopologyQuery() throws Exception {
        Method topology = ZLinkRegistryQueryClient.class.getMethod(
            "topologyAsync",
            ZLinkRegistryTopologyFilter.class);
        Method topologyAll = ZLinkRegistryQueryClient.class.getMethod(
            "topologyAsync");

        assertEquals(CompletionStage.class, topology.getReturnType());
        assertEquals(CompletionStage.class, topologyAll.getReturnType());
        assertEquals(List.of("close", "topologyAsync", "topologyAsync"),
            java.util.Arrays.stream(ZLinkRegistryQueryClient.class.getMethods())
                .filter(method -> method.getDeclaringClass() == ZLinkRegistryQueryClient.class)
                .map(Method::getName)
                .sorted()
                .toList());
    }
}
