package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.OptionalLong;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

/**
 * Complete provider SPI for descriptor, owner-lease and authority state.
 * Providers implement one store rather than composing partial capabilities.
 */
public interface ZLinkLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> listMeshNodes(
        String meshName,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationWriteResult> updateClientServer(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteStatus> removeClientServer(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        listClientServers(String channelName, ZLinkPageRequest page);

    CompletionStage<ZLinkLocationWriteResult> updateFanoutPublisher(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteStatus> removeFanoutPublisher(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>
        listFanoutPublishers(String channelName, ZLinkPageRequest page);

    CompletionStage<ZLinkOwnerLeaseClaimResult> claimOwnerLease(
        String ownerId,
        Duration leaseTtl);

    CompletionStage<ZLinkOwnerLeaseReadResult> readOwnerLease(String ownerId);

    CompletionStage<ZLinkOwnerLeaseRenewResult> renewOwnerLease(
        ZLinkLocationOwnerToken token,
        Duration leaseTtl);

    CompletionStage<ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(
        ZLinkLocationOwnerToken token);

    CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectRejectResult> reject(
        ZLinkObjectReservation reservation,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkCreationTerminalReadResult> readCreationTerminal(
        ZLinkCreationOperationIdentity operation,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationCapacityReserveResult>
        reserveRelocationCapacity(
            ZLinkRelocationCapacityReservationRequest request,
            ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationCapacityAbortResult>
        abortRelocationCapacity(
            ZLinkRelocationCapacityFence fence,
            ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation);

    CompletionStage<Long> removeAllByOwner(ZLinkLocationOwnerToken owner);

    /**
     * Returns a counter that changes when a MeshNode descriptor in the mesh
     * changes. Providers may return an empty value when they do not maintain
     * this polling optimization.
     */
    default CompletionStage<OptionalLong> getMeshNodeChangeStamp(
        String meshName) {
        return CompletableFuture.completedFuture(OptionalLong.empty());
    }
}
