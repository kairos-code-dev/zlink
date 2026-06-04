package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.registry.ZLinkRegistryQueryFilter;
import systems.zlink.framework.registry.ZLinkRegistryStatus;

final class RegistryContractTest {
    @Test
    void registryQueryUsesTypedSnapshotContracts() throws Exception {
        Method status = ZLinkRegistryQuery.class.getMethod("statusAsync");
        Method serviceSummary = ZLinkRegistryQuery.class.getMethod(
            "serviceSummaryAsync",
            ZLinkRegistryQueryFilter.class);
        Method topology = ZLinkRegistryQuery.class.getMethod(
            "topologyAsync",
            ZLinkRegistryQueryFilter.class);

        assertEquals(CompletionStage.class, status.getReturnType());
        assertEquals(CompletionStage.class, serviceSummary.getReturnType());
        assertEquals(CompletionStage.class, topology.getReturnType());
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
    void registryQueryClientOnlyExposesRemoteTopologyQuery() throws Exception {
        Method topology = ZLinkRegistryQueryClient.class.getMethod(
            "topologyAsync",
            ZLinkRegistryQueryFilter.class);

        assertEquals(CompletionStage.class, topology.getReturnType());
        assertEquals(List.of("close", "topologyAsync"),
            java.util.Arrays.stream(ZLinkRegistryQueryClient.class.getMethods())
                .filter(method -> method.getDeclaringClass() == ZLinkRegistryQueryClient.class)
                .map(Method::getName)
                .sorted()
                .toList());
    }
}
