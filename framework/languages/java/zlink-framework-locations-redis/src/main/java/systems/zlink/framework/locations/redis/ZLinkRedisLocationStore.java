package systems.zlink.framework.locations.redis;

import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkClientServerServerDescriptor;
import systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey;
import systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor;
import systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptor;
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey;
import systems.zlink.framework.runtime.internal.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.runtime.internal.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkAuthorityReadResult;
import systems.zlink.framework.locations.ZLinkAuthorityWriteResult;
import systems.zlink.framework.locations.ZLinkAuthorityExpectation;
import systems.zlink.framework.locations.ZLinkAuthorityMutation;
import systems.zlink.framework.locations.ZLinkAuthorityScanCursor;
import systems.zlink.framework.locations.ZLinkAuthorityScanResult;
import systems.zlink.framework.locations.ZLinkObjectReservationRequest;
import systems.zlink.framework.locations.ZLinkObjectReserveResult;
import systems.zlink.framework.locations.ZLinkObjectReservation;
import systems.zlink.framework.locations.ZLinkObjectCommitResult;
import systems.zlink.framework.locations.ZLinkObjectAbortResult;
import systems.zlink.framework.locations.ZLinkAggregatePrepareRequest;
import systems.zlink.framework.locations.ZLinkAggregatePrepareResult;
import systems.zlink.framework.locations.ZLinkAggregateFence;
import systems.zlink.framework.locations.ZLinkAggregateCommitResult;
import systems.zlink.framework.locations.ZLinkAggregateAbortResult;
import systems.zlink.framework.locations.ZLinkStoreCancellation;

public final class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    AutoCloseable {

    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationScriptsClient scripts;
    private final ZLinkRedisLocationRows rows;
    private final ZLinkRedisAuthorityClient authority;

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options) {
        ZLinkRedisLocationOptions validated = Objects.requireNonNull(options, "options");
        validated.validate();
        ZLinkRedisLocationKeys keys = new ZLinkRedisLocationKeys(validated.keyPrefix());
        this.connection = new ZLinkRedisLocationConnection(
            validated,
            keys.schemaKey());
        this.scripts = new ZLinkRedisLocationScriptsClient(connection, keys);
        this.rows = new ZLinkRedisLocationRows(connection, keys);
        this.authority = new ZLinkRedisAuthorityClient(connection, keys);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent) {
        return scripts.writeMeshNode(
            Objects.requireNonNull(descriptor, "descriptor"),
            Objects.requireNonNull(intent, "intent"));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key,
        systems.zlink.framework.locations.ZLinkLocationOwnerToken owner) {
        return scripts.removeMeshNode(
            Objects.requireNonNull(key, "key"),
            Objects.requireNonNull(owner, "owner"));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>
        listMeshNodes(
            String meshName,
            ZLinkPageRequest page) {
        Objects.requireNonNull(meshName, "meshName");
        return rows.listPage(
            "mesh-node",
            ZLinkRedisLocationRowJson::deserializeMeshNode,
            row -> row.meshName().equals(meshName),
            page).thenCompose(authority::projectMeshNodeCapacity);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult>
        updateClientServer(
            ZLinkClientServerServerDescriptor descriptor,
            ZLinkLocationWriteIntent intent) {
        return scripts.writeClientServer(
            Objects.requireNonNull(descriptor, "descriptor"),
            Objects.requireNonNull(intent, "intent"));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteStatus>
        removeClientServer(
            ZLinkClientServerServerDescriptorKey key,
            ZLinkLocationOwnerToken owner) {
        return scripts.removeClientServer(
            Objects.requireNonNull(key, "key"),
            Objects.requireNonNull(owner, "owner"));
    }

    @Override
    public CompletionStage<
        ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        listClientServers(
            String channelName,
            ZLinkPageRequest page) {
        if (channelName == null
            || channelName.isBlank()
            || channelName.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(
                "channelName must be non-blank text without NUL");
        }
        return rows.listClientServers(channelName, page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult>
        updateFanoutPublisher(
            ZLinkFanoutPublisherDescriptor descriptor,
            ZLinkLocationWriteIntent intent) {
        return scripts.writeFanoutPublisher(
            Objects.requireNonNull(descriptor, "descriptor"),
            Objects.requireNonNull(intent, "intent"));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteStatus>
        removeFanoutPublisher(
            ZLinkFanoutPublisherDescriptorKey key,
            ZLinkLocationOwnerToken owner) {
        return scripts.removeFanoutPublisher(
            Objects.requireNonNull(key, "key"),
            Objects.requireNonNull(owner, "owner"));
    }

    @Override
    public CompletionStage<
        ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>
        listFanoutPublishers(
            String channelName,
            ZLinkPageRequest page) {
        if (channelName == null
            || channelName.isBlank()
            || channelName.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(
                "channelName must be non-blank text without NUL");
        }
        return rows.listFanoutPublishers(channelName, page);
    }

    CompletionStage<byte[]> readAuthorityMembershipMutation(
        String authorityKey) {
        return authority.readMembershipMutation(authorityKey);
    }

    CompletionStage<java.util.Map<String, String>>
        readMeshNodeHashFields(
            ZLinkMeshNodeDescriptorKey key) {
        return scripts.readMeshNodeHashFields(key);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>
        claimOwnerLease(
        String ownerId,
        Duration leaseTtl) {
        return scripts.claimOwnerLeaseAsync(ownerId, leaseTtl);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult>
        readOwnerLease(String ownerId) {
        return scripts.readOwnerLeaseAsync(ownerId);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>
        renewOwnerLease(
            systems.zlink.framework.locations.ZLinkLocationOwnerToken token,
            Duration leaseTtl) {
        return scripts.renewOwnerLeaseAsync(token, leaseTtl);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult>
        releaseOwnerLease(
            systems.zlink.framework.locations.ZLinkLocationOwnerToken token) {
        return scripts.releaseOwnerLeaseAsync(token);
    }

    @Override
    public CompletionStage<Long> removeAllByOwner(
        ZLinkLocationOwnerToken owner) {
        return scripts.removeAllByOwnerAsync(owner);
    }

    @Override
    public CompletionStage<OptionalLong> getMeshNodeChangeStamp(
        String meshName) {
        return scripts.getMeshNodeChangeStamp(meshName);
    }

    @Override
    public CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation) {
        return authority.read(key, cancellation);
    }

    @Override
    public CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation) {
        return authority.compareExchange(key, expectation, mutation, cancellation);
    }

    @Override
    public CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation) {
        return authority.list(prefix, cursor, limit, cancellation);
    }

    @Override
    public CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation) {
        return authority.reserve(request, cancellation);
    }

    @Override
    public CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkStoreCancellation cancellation) {
        return authority.commit(reservation, readyPayload, cancellation);
    }

    @Override
    public CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation) {
        return authority.commit(
            reservation,
            readyPayload,
            terminal,
            cancellation);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkObjectRejectResult> reject(
        ZLinkObjectReservation reservation,
        systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation) {
        return authority.reject(reservation, terminal, cancellation);
    }

    @Override
    public CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation) {
        return authority.abort(reservation, cancellation);
    }

    @Override
    public CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        systems.zlink.framework.locations.ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation) {
        return authority.abort(reservation, terminal, cancellation);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkCreationTerminalReadResult>
        readCreationTerminal(
            systems.zlink.framework.locations.ZLinkCreationOperationIdentity operation,
            ZLinkStoreCancellation cancellation) {
        return authority.readCreationTerminal(operation, cancellation);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult>
        reserveRelocationCapacity(
            systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest request,
            ZLinkStoreCancellation cancellation) {
        return authority.reserveRelocationCapacity(request, cancellation);
    }

    @Override
    public CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult>
        abortRelocationCapacity(
            systems.zlink.framework.locations.ZLinkRelocationCapacityFence fence,
            ZLinkStoreCancellation cancellation) {
        return authority.abortRelocationCapacity(fence, cancellation);
    }

    @Override
    public CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation) {
        return authority.prepareAggregate(request, cancellation);
    }

    @Override
    public CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        return authority.commitAggregate(fence, cancellation);
    }

    @Override
    public CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        return authority.abortAggregate(fence, cancellation);
    }

    @Override
    public void close() {
        connection.closeAsync().toCompletableFuture().join();
    }
}
