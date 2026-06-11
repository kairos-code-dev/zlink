package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.lang.reflect.Method;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.RegistryState;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.contracts.service.spot.SpotNodeState;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.framework.registry.ZLinkMemberPeerEntry;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
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
        Method status = ZLinkRegistryQuery.class.getMethod("status");
        Method serviceSummary = ZLinkRegistryQuery.class.getMethod(
            "serviceSummary",
            ZLinkRegistryServiceSummaryFilter.class);
        Method serviceSummaryAll = ZLinkRegistryQuery.class.getMethod(
            "serviceSummary");
        Method topology = ZLinkRegistryQuery.class.getMethod(
            "topology",
            ZLinkRegistryTopologyFilter.class);
        Method topologyAll = ZLinkRegistryQuery.class.getMethod(
            "topology");
        Method memberPeers = ZLinkRegistryQuery.class.getMethod(
            "memberPeers",
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
        assertEquals(RegistryState.class, componentType(ZLinkRegistryStatus.class, "state"));
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
        assertEquals(
            AutoConnectType.class,
            componentType(ZLinkRegistryServiceSummaryEntry.class, "autoConnectType"));
        assertEquals(
            ServiceRole.class,
            componentType(ZLinkRegistryServiceSummaryEntry.class, "serviceRole"));
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
        assertEquals(
            AutoConnectType.class,
            componentType(ZLinkRegistryTopologyEntry.class, "autoConnectType"));
        assertEquals(ServiceKind.class, componentType(ZLinkRegistryTopologyEntry.class, "serviceKind"));
        assertEquals(ServiceRole.class, componentType(ZLinkRegistryTopologyEntry.class, "serviceRole"));
        assertEquals(TopologySource.class, componentType(ZLinkRegistryTopologyEntry.class, "source"));
        assertEquals(TopologyState.class, componentType(ZLinkRegistryTopologyEntry.class, "state"));
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
        assertEquals(
            AutoConnectType.class,
            componentType(ZLinkMemberPeerEntry.class, "autoConnectType"));
        assertEquals(ServiceRole.class, componentType(ZLinkMemberPeerEntry.class, "serviceRole"));
    }

    @Test
    void registryFiltersExposeTypedEnumCriteria() {
        assertEquals(
            Optional.class,
            componentType(ZLinkRegistryServiceSummaryFilter.class, "autoConnectType"));
        assertEquals(
            Optional.class,
            componentType(ZLinkRegistryTopologyFilter.class, "serviceKind"));

        ZLinkRegistryServiceSummaryFilter summaryFilter =
            new ZLinkRegistryServiceSummaryFilter(
                Optional.of(AutoConnectType.CLIENT_SERVER),
                Optional.of(ServiceRole.DEALER),
                Optional.of("profile"));
        ZLinkRegistryTopologyFilter topologyFilter =
            new ZLinkRegistryTopologyFilter(
                Optional.of(AutoConnectType.CLIENT_SERVER),
                Optional.of(ServiceKind.SOCKET),
                Optional.of(ServiceRole.ROUTER),
                Optional.of("profile"),
                Optional.empty(),
                Optional.of(TopologyState.READY),
                Optional.of(TopologySource.MANUAL));

        assertEquals(AutoConnectType.CLIENT_SERVER, summaryFilter.autoConnectType().orElseThrow());
        assertEquals(ServiceKind.SOCKET, topologyFilter.serviceKind().orElseThrow());
        assertEquals(TopologyState.READY, topologyFilter.state().orElseThrow());
    }

    @Test
    void monitoringEventsCarryTypedSnapshots() {
        assertEquals(
            Optional.class,
            componentType(ZLinkRegistryEvent.class, "status"));
        assertEquals(List.class, componentType(ZLinkRegistryEvent.class, "topology"));
        assertEquals(
            Optional.class,
            componentType(ZLinkSpotEvent.class, "status"));

        ZLinkSpotEvent spotEvent = new ZLinkSpotEvent(
            "play",
            Instant.EPOCH,
            ZLinkSpotEventKind.STATUS_CHANGED,
            Optional.of(new SpotNodeStatus(
                "play",
                "inproc://play",
                null,
                SpotNodeState.READY,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0)),
            null,
            null,
            null);
        assertEquals(
            SpotNodeState.READY,
            spotEvent.status().orElseThrow().state());
        assertEquals(List.of(), spotEvent.peers());
        assertEquals(Optional.empty(), spotEvent.timerDiagnostic());
    }

    @Test
    void registryQueryClientOnlyExposesRemoteTopologyQuery() throws Exception {
        Method topology = ZLinkRegistryQueryClient.class.getMethod(
            "topology",
            ZLinkRegistryTopologyFilter.class);
        Method topologyAll = ZLinkRegistryQueryClient.class.getMethod(
            "topology");

        assertEquals(CompletionStage.class, topology.getReturnType());
        assertEquals(CompletionStage.class, topologyAll.getReturnType());
        assertEquals(List.of("close", "topology", "topology"),
            java.util.Arrays.stream(ZLinkRegistryQueryClient.class.getMethods())
                .filter(method -> method.getDeclaringClass() == ZLinkRegistryQueryClient.class)
                .map(Method::getName)
                .sorted()
                .toList());
    }

    private static Class<?> componentType(Class<? extends Record> type, String name) {
        return java.util.Arrays.stream(type.getRecordComponents())
            .filter(component -> component.getName().equals(name))
            .findFirst()
            .orElseThrow()
            .getType();
    }
}
