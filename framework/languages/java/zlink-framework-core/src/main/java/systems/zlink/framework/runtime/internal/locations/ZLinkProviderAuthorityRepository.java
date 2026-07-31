package systems.zlink.framework.runtime.internal.locations;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locationprovider.*;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;

/**
 * Implements Framework-owned authority records over the opaque provider SPI.
 */
final class ZLinkProviderAuthorityRepository {
    private static final String PREFIX = "zlink:v11:authority:";
    private static final String COUNTER_PREFIX = "zlink:v11:counter:";
    private static final int CODEC_VERSION = 1;
    private final systems.zlink.framework.locationprovider.ZLinkLocationStore
        provider;

    ZLinkProviderAuthorityRepository(
        systems.zlink.framework.locationprovider.ZLinkLocationStore provider) {
        this.provider = Objects.requireNonNull(provider, "provider");
    }

    CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation) {
        requireKey(key);
        return provider.read(authorityKey(key), adapt(cancellation))
            .thenApply(result -> toRead(result));
    }

    CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation) {
        requireKey(key);
        Objects.requireNonNull(expectation, "expectation");
        Objects.requireNonNull(mutation, "mutation");
        var opaqueCancellation = adapt(cancellation);
        ZLinkStoreKey rowKey = authorityKey(key);
        return provider.read(rowKey, opaqueCancellation)
            .thenCompose(read -> {
                if (!(read instanceof ZLinkStoreReadFound found)
                    || !(expectation instanceof ZLinkAuthorityExpectFound expected)
                    || !found.value().version().value().equals(
                        expected.storeVersion())) {
                    return completed(new ZLinkAuthorityConflict(toRead(read)));
                }
                AuthorityRecord current = decode(found.value().bytes());
                List<ZLinkStoreCondition> conditions = new ArrayList<>();
                conditions.add(new ZLinkStoreVersionCondition(
                    rowKey, found.value().version()));
                if (mutation instanceof ZLinkAuthorityDelete) {
                    return requireLiveOwner(current, conditions,
                            opaqueCancellation)
                        .thenCompose(live -> {
                            if (!live) {
                                return completed(
                                    new ZLinkAuthorityConflict(toRead(read)));
                            }
                            return provider.write(
                                    new ZLinkStoreWriteRequest(
                                        conditions,
                                        List.of(new ZLinkStoreDelete(rowKey))),
                                    opaqueCancellation)
                                .thenApply(result -> result
                                    instanceof ZLinkStoreWriteApplied applied
                                    ? new ZLinkAuthorityDeleted(
                                        found.value().version().value(),
                                        applied.storeNow())
                                    : new ZLinkAuthorityConflict(toRead(read)));
                        });
                }
                if (mutation instanceof ZLinkAuthorityRestore restore) {
                    if (!current.ownerId().equals(
                            restore.expectedOwner().ownerId())
                        || current.ownerLeaseGeneration()
                            != restore.expectedOwner().leaseGeneration()) {
                        return completed(
                            new ZLinkAuthorityConflict(toRead(read)));
                    }
                    AuthorityRecord next = current.withPayload(
                        restore.payload());
                    return put(rowKey, found, next, conditions,
                        opaqueCancellation, read);
                }
                ZLinkAuthorityPut put = (ZLinkAuthorityPut) mutation;
                ZLinkLocationOwnerToken target = put.targetOwner()
                    .orElse(new ZLinkLocationOwnerToken(
                        current.ownerId(),
                        current.ownerLeaseGeneration()));
                return requireLiveOwner(target, conditions,
                        opaqueCancellation)
                    .thenCompose(live -> {
                        if (!live) {
                            return completed(
                                new ZLinkAuthorityConflict(toRead(read)));
                        }
                        if (put.generationTransition()
                            == ZLinkAuthorityGenerationTransition.NEW_OWNER) {
                            return nextCounter(
                                    "authority-owner",
                                    conditions,
                                    opaqueCancellation)
                                .thenCompose(counter -> {
                                    AuthorityRecord next = current.withOwner(
                                        put.payload(),
                                        counter.value(),
                                        target);
                                    List<ZLinkStoreMutation> mutations =
                                        new ArrayList<>(counter.mutations());
                                    mutations.add(new ZLinkStorePut(
                                        rowKey, encode(next), null));
                                    return writeAuthority(
                                        rowKey, found, next, conditions,
                                        mutations, opaqueCancellation, read);
                                });
                        }
                        return put(
                            rowKey,
                            found,
                            current.withPayload(put.payload()),
                            conditions,
                            opaqueCancellation,
                            read);
                    });
            });
    }

    CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation) {
        if (limit <= 0) {
            throw new IllegalArgumentException(
                "authority scan limit must be positive");
        }
        ZLinkStoreScanCursor providerCursor = cursor
            .map(value -> new ZLinkStoreScanCursor(value.encoded()))
            .orElse(null);
        return provider.scan(
                new ZLinkStoreScanRequest(
                    PREFIX,
                    providerCursor,
                    limit),
                adapt(cancellation))
            .thenApply(result -> {
                if (result instanceof ZLinkStoreScanExpired) {
                    return new ZLinkAuthorityScanExpired();
                }
                ZLinkStoreScanPage page =
                    ((ZLinkStoreScanPageResult) result).value();
                List<ZLinkAuthorityEntry> items = page.items().stream()
                    .map(item -> new DecodedItem(
                        decodeAuthorityKey(item.key()),
                        item.value()))
                    .filter(item -> item.key().startsWith(prefix))
                    .map(item -> new ZLinkAuthorityEntry(
                        item.key(),
                        snapshot(item.value())))
                    .toList();
                return new ZLinkAuthorityPage(
                    items,
                    page.nextCursor() == null
                        ? Optional.empty()
                        : Optional.of(new ZLinkAuthorityScanCursor(
                            page.nextCursor().value())));
            });
    }

    CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        var opaqueCancellation = adapt(cancellation);
        ZLinkStoreKey key = authorityKey(request.authorityKey());
        return provider.read(key, opaqueCancellation).thenCompose(read -> {
            if (read instanceof ZLinkStoreReadFound found) {
                ZLinkAuthoritySnapshot current = snapshot(found.value());
                return completed(
                    current.allocation().stableType().equals(
                        request.stableType())
                        && current.allocation().state()
                            == ZLinkPlacementAllocationState.ACTIVE
                        ? new ZLinkObjectAlreadyExists(current)
                        : current.allocation().stableType().equals(
                            request.stableType())
                            ? new ZLinkObjectConflict(current)
                            : new ZLinkObjectTypeMismatch(current));
            }
            List<ZLinkStoreCondition> conditions = new ArrayList<>();
            conditions.add(new ZLinkStoreMissingCondition(key));
            return requireLiveOwner(
                    request.targetOwner(),
                    conditions,
                    opaqueCancellation)
                .thenCompose(live -> {
                    if (!live) {
                        return completed(new ZLinkObjectConflict(
                            toRead(read)));
                    }
                    return nextPair(
                            conditions,
                            opaqueCancellation)
                        .thenCompose(counters -> {
                            String reservationVersion =
                                java.util.UUID.randomUUID().toString();
                            AuthorityRecord record = new AuthorityRecord(
                                request.creatingPayload(),
                                counters.objectGeneration(),
                                counters.ownerGeneration(),
                                request.targetOwner().ownerId(),
                                request.targetOwner().leaseGeneration(),
                                new ZLinkPlacementAllocation(
                                    ZLinkPlacementAllocationState.PENDING,
                                    request.objectKind(),
                                    request.stableType(),
                                    request.targetDescriptor(),
                                    request
                                        .targetDescriptorLifecycleGeneration(),
                                    request.capacityBundle()),
                                Optional.of(new ZLinkPendingObjectCreation(
                                    reservationVersion,
                                    request.creationIntentReference(),
                                    request.creationIntentHash(),
                                    request.creationIntentEncodedSize())));
                            List<ZLinkStoreMutation> mutations =
                                new ArrayList<>(counters.mutations());
                            mutations.add(new ZLinkStorePut(
                                key, encode(record), null));
                            return provider.write(
                                    new ZLinkStoreWriteRequest(
                                        conditions, mutations),
                                    opaqueCancellation)
                                .thenCompose(result -> {
                                    if (result
                                        instanceof ZLinkStoreWriteConflict) {
                                        return reserve(request, cancellation);
                                    }
                                    var applied =
                                        (ZLinkStoreWriteApplied) result;
                                    return completed(
                                        new ZLinkObjectReserved(
                                            new ZLinkObjectReservation(
                                                request.authorityKey(),
                                                applied.putVersions()
                                                    .get(key).value(),
                                                counters.objectGeneration(),
                                                counters.ownerGeneration(),
                                                reservationVersion,
                                                request.targetDescriptor(),
                                                request
                                                    .targetDescriptorLifecycleGeneration(),
                                                request.targetOwner())));
                                });
                        });
                });
        });
    }

    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(reservation, "reservation");
        Objects.requireNonNull(readyPayload, "readyPayload");
        ZLinkStoreKey key = authorityKey(reservation.authorityKey());
        var opaqueCancellation = adapt(cancellation);
        return provider.read(key, opaqueCancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            AuthorityRecord current = decode(found.value().bytes());
            if (!matches(current, reservation)) {
                return completed(
                    current.pendingCreation().isEmpty()
                        ? ZLinkObjectCommitResult.ALREADY_COMMITTED
                        : ZLinkObjectCommitResult.STALE);
            }
            AuthorityRecord next = new AuthorityRecord(
                readyPayload,
                current.objectGeneration(),
                current.authorityOwnerGeneration(),
                current.ownerId(),
                current.ownerLeaseGeneration(),
                withState(
                    current.allocation(),
                    ZLinkPlacementAllocationState.ACTIVE),
                Optional.empty());
            return provider.write(
                    new ZLinkStoreWriteRequest(
                        List.of(new ZLinkStoreVersionCondition(
                            key, found.value().version())),
                        List.of(new ZLinkStorePut(key, encode(next), null))),
                    opaqueCancellation)
                .thenApply(result -> result
                    instanceof ZLinkStoreWriteApplied
                    ? ZLinkObjectCommitResult.COMMITTED
                    : ZLinkObjectCommitResult.STALE);
        });
    }

    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation) {
        ZLinkStoreKey key = authorityKey(reservation.authorityKey());
        var opaqueCancellation = adapt(cancellation);
        return provider.read(key, opaqueCancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)
                || !matches(decode(found.value().bytes()), reservation)) {
                return completed(ZLinkObjectAbortResult.STALE);
            }
            return provider.write(
                    new ZLinkStoreWriteRequest(
                        List.of(new ZLinkStoreVersionCondition(
                            key, found.value().version())),
                        List.of(new ZLinkStoreDelete(key))),
                    opaqueCancellation)
                .thenApply(result -> result
                    instanceof ZLinkStoreWriteApplied
                    ? ZLinkObjectAbortResult.ABORTED
                    : ZLinkObjectAbortResult.STALE);
        });
    }

    CompletionStage<ZLinkObjectRejectResult> reject(
        ZLinkObjectReservation reservation,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(terminal, "terminal");
        return abort(reservation, cancellation).thenApply(result -> switch (
            result) {
            case ABORTED -> ZLinkObjectRejectResult.REJECTED;
            case ALREADY_ABORTED -> ZLinkObjectRejectResult.ALREADY_REJECTED;
            case STALE -> ZLinkObjectRejectResult.STALE;
        });
    }

    CompletionStage<ZLinkCreationTerminalReadResult> readCreationTerminal(
        ZLinkCreationOperationIdentity operation,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(operation, "operation");
        return completed(new ZLinkCreationTerminalMissing());
    }

    CompletionStage<ZLinkRelocationCapacityReserveResult>
        reserveRelocationCapacity(
            ZLinkRelocationCapacityReservationRequest request,
            ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        var opaqueCancellation = adapt(cancellation);
        ZLinkRelocationCapacityFence fence =
            new ZLinkRelocationCapacityFence(
                request.reservationId().toString());
        ZLinkStoreKey marker = relocationKey(fence);
        return provider.read(
                authorityKey(request.authorityKey()),
                opaqueCancellation)
            .thenCompose(authority -> {
                if (!(authority instanceof ZLinkStoreReadFound found)
                    || !found.value().version().value().equals(
                        request.expectedStoreVersion())) {
                    return completed(
                        new ZLinkRelocationCapacityConflict(
                            toRead(authority)));
                }
                return provider.write(
                        new ZLinkStoreWriteRequest(
                            List.of(
                                new ZLinkStoreMissingCondition(marker),
                                new ZLinkStoreVersionCondition(
                                    authorityKey(request.authorityKey()),
                                    found.value().version())),
                            List.of(new ZLinkStorePut(
                                marker,
                                new byte[] {1},
                                null))),
                        opaqueCancellation)
                    .thenCompose(result -> {
                        if (result instanceof ZLinkStoreWriteApplied) {
                            return completed(
                                new ZLinkRelocationCapacityReserved(fence));
                        }
                        return provider.read(marker, opaqueCancellation)
                            .thenApply(existing -> existing
                                instanceof ZLinkStoreReadFound
                                ? new ZLinkRelocationCapacityAlreadyReserved(
                                    fence)
                                : new ZLinkRelocationCapacityConflict(
                                    toRead(authority)));
                    });
            });
    }

    CompletionStage<ZLinkRelocationCapacityAbortResult>
        abortRelocationCapacity(
            ZLinkRelocationCapacityFence fence,
            ZLinkStoreCancellation cancellation) {
        ZLinkStoreKey key = relocationKey(fence);
        var opaqueCancellation = adapt(cancellation);
        return provider.read(key, opaqueCancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.ALREADY_ABORTED);
            }
            if (found.value().bytes()[0] == 2) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.ALREADY_COMMITTED);
            }
            return provider.write(
                    new ZLinkStoreWriteRequest(
                        List.of(new ZLinkStoreVersionCondition(
                            key, found.value().version())),
                        List.of(new ZLinkStoreDelete(key))),
                    opaqueCancellation)
                .thenApply(result -> result
                    instanceof ZLinkStoreWriteApplied
                    ? ZLinkRelocationCapacityAbortResult.ABORTED
                    : ZLinkRelocationCapacityAbortResult.STALE);
        });
    }

    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        ZLinkAggregateFence fence = new ZLinkAggregateFence(
            request.aggregateId(), request.aggregateGeneration());
        ZLinkStoreKey key = aggregateKey(fence);
        var opaqueCancellation = adapt(cancellation);
        return provider.read(key, opaqueCancellation).thenCompose(existing -> {
            if (existing instanceof ZLinkStoreReadFound) {
                return completed(new ZLinkAggregateAlreadyPrepared(fence));
            }
            CompletionStage<Boolean> valid = completed(true);
            for (ZLinkAggregateParticipant participant
                : request.participants()) {
                valid = valid.thenCompose(ok -> {
                    if (!ok) {
                        return completed(false);
                    }
                    return provider.read(
                            authorityKey(participant.authorityKey()),
                            opaqueCancellation)
                        .thenApply(read ->
                            read instanceof ZLinkStoreReadFound found
                                && found.value().version().value().equals(
                                    participant.expectedStoreVersion()));
                });
            }
            return valid.thenCompose(ok -> {
                if (!ok) {
                    return completed(new ZLinkAggregateConflict());
                }
                return provider.write(
                        new ZLinkStoreWriteRequest(
                            List.of(new ZLinkStoreMissingCondition(key)),
                            List.of(new ZLinkStorePut(
                                key, encodeAggregate((byte) 1, request), null))),
                        opaqueCancellation)
                    .thenApply(result -> result
                        instanceof ZLinkStoreWriteApplied
                        ? new ZLinkAggregatePrepared(fence)
                        : new ZLinkAggregateAlreadyPrepared(fence));
            });
        });
    }

    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        ZLinkStoreKey marker = aggregateKey(fence);
        var opaqueCancellation = adapt(cancellation);
        return provider.read(marker, opaqueCancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)) {
                return completed(ZLinkAggregateCommitResult.STALE);
            }
            PreparedAggregate prepared =
                decodeAggregate(found.value().bytes());
            if (prepared.state() == 2) {
                return completed(
                    ZLinkAggregateCommitResult.ALREADY_COMMITTED);
            }
            ZLinkAggregatePrepareRequest request = prepared.request();
            return loadParticipants(
                    request.participants(),
                    opaqueCancellation)
                .thenCompose(rows -> {
                    if (rows.size() != request.participants().size()) {
                        return completed(ZLinkAggregateCommitResult.STALE);
                    }
                    List<ZLinkStoreCondition> conditions =
                        new ArrayList<>();
                    conditions.add(new ZLinkStoreVersionCondition(
                        marker, found.value().version()));
                    return requireLiveOwner(
                            request.targetOwner(),
                            conditions,
                            opaqueCancellation)
                        .thenCompose(live -> {
                            if (!live) {
                                return completed(
                                    ZLinkAggregateCommitResult.STALE);
                            }
                            int changes = Math.toIntExact(
                                request.participants().stream()
                                    .filter(participant ->
                                        participant.ownerTransition()
                                            == ZLinkAuthorityGenerationTransition
                                                .NEW_OWNER)
                                    .count());
                            return nextCounterRange(
                                    "authority-owner",
                                    changes,
                                    conditions,
                                    opaqueCancellation)
                                .thenCompose(counter -> {
                                    List<ZLinkStoreMutation> mutations =
                                        new ArrayList<>(
                                            counter.mutations());
                                    long ownerGeneration = counter.value();
                                    for (int index = 0;
                                         index < rows.size();
                                         index++) {
                                        LoadedParticipant loaded =
                                            rows.get(index);
                                        ZLinkAggregateParticipant participant =
                                            request.participants().get(index);
                                        if (!loaded.value().version().value()
                                                .equals(participant
                                                    .expectedStoreVersion())) {
                                            return completed(
                                                ZLinkAggregateCommitResult
                                                    .STALE);
                                        }
                                        conditions.add(
                                            new ZLinkStoreVersionCondition(
                                                loaded.key(),
                                                loaded.value().version()));
                                        AuthorityRecord current =
                                            decode(loaded.value().bytes());
                                        boolean moves =
                                            participant.ownerTransition()
                                                == ZLinkAuthorityGenerationTransition
                                                    .NEW_OWNER;
                                        ZLinkPlacementAllocation allocation =
                                            moves
                                                ? new ZLinkPlacementAllocation(
                                                    ZLinkPlacementAllocationState
                                                        .ACTIVE,
                                                    current.allocation()
                                                        .objectKind(),
                                                    current.allocation()
                                                        .stableType(),
                                                    request.targetDescriptor(),
                                                    request
                                                        .targetDescriptorLifecycleGeneration(),
                                                    current.allocation()
                                                        .capacityBundle())
                                                : current.allocation();
                                        AuthorityRecord next =
                                            new AuthorityRecord(
                                                participant.authorityPayload(),
                                                current.objectGeneration(),
                                                moves
                                                    ? ownerGeneration++
                                                    : current
                                                        .authorityOwnerGeneration(),
                                                moves
                                                    ? request.targetOwner()
                                                        .ownerId()
                                                    : current.ownerId(),
                                                moves
                                                    ? request.targetOwner()
                                                        .leaseGeneration()
                                                    : current
                                                        .ownerLeaseGeneration(),
                                                allocation,
                                                Optional.empty());
                                        mutations.add(new ZLinkStorePut(
                                            loaded.key(),
                                            encode(next),
                                            null));
                                    }
                                    mutations.add(new ZLinkStorePut(
                                        marker,
                                        encodeAggregate((byte) 2, request),
                                        null));
                                    return provider.write(
                                            new ZLinkStoreWriteRequest(
                                                conditions, mutations),
                                            opaqueCancellation)
                                        .thenApply(result -> result
                                            instanceof ZLinkStoreWriteApplied
                                            ? ZLinkAggregateCommitResult
                                                .COMMITTED
                                            : ZLinkAggregateCommitResult
                                                .STALE);
                                });
                        });
                });
        });
    }

    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        ZLinkStoreKey key = aggregateKey(fence);
        var opaqueCancellation = adapt(cancellation);
        return provider.read(key, opaqueCancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)) {
                return completed(ZLinkAggregateAbortResult.ALREADY_ABORTED);
            }
            if (decodeAggregate(found.value().bytes()).state() == 2) {
                return completed(ZLinkAggregateAbortResult.STALE);
            }
            return provider.write(
                    new ZLinkStoreWriteRequest(
                        List.of(new ZLinkStoreVersionCondition(
                            key, found.value().version())),
                        List.of(new ZLinkStoreDelete(key))),
                    opaqueCancellation)
                .thenApply(result -> result
                    instanceof ZLinkStoreWriteApplied
                    ? ZLinkAggregateAbortResult.ABORTED
                    : ZLinkAggregateAbortResult.STALE);
        });
    }

    CompletionStage<Long> removeAllByOwner(
        ZLinkLocationOwnerToken owner) {
        return provider.scan(
                new ZLinkStoreScanRequest(PREFIX, null, 1000),
                () -> false)
            .thenCompose(result -> {
                if (!(result instanceof ZLinkStoreScanPageResult pageResult)) {
                    return completed(0L);
                }
                List<ZLinkStoreCondition> conditions = new ArrayList<>();
                List<ZLinkStoreMutation> mutations = new ArrayList<>();
                for (ZLinkStoreScanItem item
                    : pageResult.value().items()) {
                    AuthorityRecord record =
                        decode(item.value().bytes());
                    if (record.ownerId().equals(owner.ownerId())
                        && record.ownerLeaseGeneration()
                            == owner.leaseGeneration()) {
                        conditions.add(new ZLinkStoreVersionCondition(
                            item.key(), item.value().version()));
                        mutations.add(new ZLinkStoreDelete(item.key()));
                    }
                }
                if (mutations.isEmpty()) {
                    return completed(0L);
                }
                return provider.write(
                        new ZLinkStoreWriteRequest(
                            conditions, mutations),
                        () -> false)
                    .thenApply(write -> write
                        instanceof ZLinkStoreWriteApplied
                        ? (long) mutations.size()
                        : 0L);
            });
    }

    private CompletionStage<List<LoadedParticipant>> loadParticipants(
        List<ZLinkAggregateParticipant> participants,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        CompletionStage<List<LoadedParticipant>> loaded =
            completed(new ArrayList<>());
        for (ZLinkAggregateParticipant participant : participants) {
            loaded = loaded.thenCompose(rows -> provider.read(
                    authorityKey(participant.authorityKey()),
                    cancellation)
                .thenApply(read -> {
                    if (read instanceof ZLinkStoreReadFound found) {
                        rows.add(new LoadedParticipant(
                            authorityKey(participant.authorityKey()),
                            found.value()));
                    }
                    return rows;
                }));
        }
        return loaded.thenApply(List::copyOf);
    }

    private CompletionStage<ZLinkAuthorityWriteResult> put(
        ZLinkStoreKey key,
        ZLinkStoreReadFound found,
        AuthorityRecord next,
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation,
        ZLinkStoreReadResult current) {
        return writeAuthority(
            key,
            found,
            next,
            conditions,
            List.of(new ZLinkStorePut(key, encode(next), null)),
            cancellation,
            current);
    }

    private CompletionStage<ZLinkAuthorityWriteResult> writeAuthority(
        ZLinkStoreKey key,
        ZLinkStoreReadFound found,
        AuthorityRecord next,
        List<ZLinkStoreCondition> conditions,
        List<ZLinkStoreMutation> mutations,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation,
        ZLinkStoreReadResult current) {
        return provider.write(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellation)
            .thenApply(result -> {
                if (!(result instanceof ZLinkStoreWriteApplied applied)) {
                    return new ZLinkAuthorityConflict(toRead(current));
                }
                ZLinkStoreVersion version =
                    applied.putVersions().get(key);
                return stored(next, version.value(), applied.storeNow());
            });
    }

    private CompletionStage<Boolean> requireLiveOwner(
        AuthorityRecord record,
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        return requireLiveOwner(
            new ZLinkLocationOwnerToken(
                record.ownerId(), record.ownerLeaseGeneration()),
            conditions,
            cancellation);
    }

    private CompletionStage<Boolean> requireLiveOwner(
        ZLinkLocationOwnerToken owner,
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        ZLinkStoreKey key = ownerKey(owner.ownerId());
        return provider.read(key, cancellation).thenApply(read -> {
            if (!(read instanceof ZLinkStoreReadFound found)
                || ownerGeneration(found.value().bytes())
                    != owner.leaseGeneration()) {
                return false;
            }
            conditions.add(new ZLinkStoreVersionCondition(
                key, found.value().version()));
            return true;
        });
    }

    private CompletionStage<Counters> nextPair(
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        return nextCounter("object", conditions, cancellation)
            .thenCompose(object -> nextCounter(
                    "authority-owner", conditions, cancellation)
                .thenApply(owner -> new Counters(
                    object.value(),
                    owner.value(),
                    concat(object.mutations(), owner.mutations()))));
    }

    private CompletionStage<Counter> nextCounter(
        String name,
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        ZLinkStoreKey key = new ZLinkStoreKey(COUNTER_PREFIX + name);
        return provider.read(key, cancellation).thenApply(read -> {
            long value = read instanceof ZLinkStoreReadFound found
                ? decodeLong(found.value().bytes())
                : 1L;
            if (value == Long.MAX_VALUE) {
                throw new IllegalStateException(
                    "Location Store generation is exhausted");
            }
            conditions.add(read instanceof ZLinkStoreReadFound found
                ? new ZLinkStoreVersionCondition(
                    key, found.value().version())
                : new ZLinkStoreMissingCondition(key));
            return new Counter(
                value,
                List.of(new ZLinkStorePut(
                    key, encodeLong(value + 1), null)));
        });
    }

    private CompletionStage<Counter> nextCounterRange(
        String name,
        int count,
        List<ZLinkStoreCondition> conditions,
        systems.zlink.framework.locationprovider.ZLinkStoreCancellation
            cancellation) {
        if (count == 0) {
            return completed(new Counter(0, List.of()));
        }
        ZLinkStoreKey key = new ZLinkStoreKey(COUNTER_PREFIX + name);
        return provider.read(key, cancellation).thenApply(read -> {
            long value = read instanceof ZLinkStoreReadFound found
                ? decodeLong(found.value().bytes())
                : 1L;
            long next = Math.addExact(value, count);
            conditions.add(read instanceof ZLinkStoreReadFound found
                ? new ZLinkStoreVersionCondition(
                    key, found.value().version())
                : new ZLinkStoreMissingCondition(key));
            return new Counter(
                value,
                List.of(new ZLinkStorePut(
                    key, encodeLong(next), null)));
        });
    }

    private static ZLinkAuthorityReadResult toRead(
        ZLinkStoreReadResult result) {
        return result instanceof ZLinkStoreReadMissing missing
            ? new ZLinkAuthorityMissing(missing.storeNow())
            : snapshot(((ZLinkStoreReadFound) result).value());
    }

    private static ZLinkAuthoritySnapshot snapshot(ZLinkStoreValue value) {
        AuthorityRecord record = decode(value.bytes());
        return new ZLinkAuthoritySnapshot(
            value.version().value(),
            record.payload(),
            record.objectGeneration(),
            record.authorityOwnerGeneration(),
            record.ownerId(),
            record.ownerLeaseGeneration(),
            record.allocation(),
            record.pendingCreation(),
            value.storeNow());
    }

    private static ZLinkAuthorityStored stored(
        AuthorityRecord record,
        String version,
        Instant storeNow) {
        return new ZLinkAuthorityStored(
            version,
            record.payload(),
            record.objectGeneration(),
            record.authorityOwnerGeneration(),
            record.ownerId(),
            record.ownerLeaseGeneration(),
            record.allocation(),
            storeNow);
    }

    private static boolean matches(
        AuthorityRecord current,
        ZLinkObjectReservation reservation) {
        return current.objectGeneration() == reservation.objectGeneration()
            && current.authorityOwnerGeneration()
                == reservation.authorityOwnerGeneration()
            && current.ownerId().equals(
                reservation.targetOwner().ownerId())
            && current.ownerLeaseGeneration()
                == reservation.targetOwner().leaseGeneration()
            && current.pendingCreation()
                .map(ZLinkPendingObjectCreation::reservationId)
                .filter(reservation.reservationVersion()::equals)
                .isPresent();
    }

    private static ZLinkPlacementAllocation withState(
        ZLinkPlacementAllocation value,
        ZLinkPlacementAllocationState state) {
        return new ZLinkPlacementAllocation(
            state,
            value.objectKind(),
            value.stableType(),
            value.descriptor(),
            value.descriptorLifecycleGeneration(),
            value.capacityBundle());
    }

    private static byte[] encode(AuthorityRecord value) {
        try {
            var bytes = new ByteArrayOutputStream();
            var out = new DataOutputStream(bytes);
            out.writeInt(CODEC_VERSION);
            writeBytes(out, value.payload());
            out.writeLong(value.objectGeneration());
            out.writeLong(value.authorityOwnerGeneration());
            out.writeUTF(value.ownerId());
            out.writeLong(value.ownerLeaseGeneration());
            ZLinkPlacementAllocation allocation = value.allocation();
            out.writeInt(allocation.state().ordinal());
            out.writeInt(allocation.objectKind().ordinal());
            out.writeUTF(allocation.stableType());
            out.writeUTF(allocation.descriptor().meshName());
            out.writeUTF(allocation.descriptor().rid().toHex());
            out.writeLong(allocation.descriptorLifecycleGeneration());
            out.writeInt(allocation.capacityBundle().actorSlots());
            out.writeInt(allocation.capacityBundle().spotSlots());
            out.writeBoolean(allocation.capacityBundle().spotType().isPresent());
            if (allocation.capacityBundle().spotType().isPresent()) {
                ZLinkSpotTypeCapacityDelta delta =
                    allocation.capacityBundle().spotType().orElseThrow();
                out.writeInt(delta.objectKind().ordinal());
                out.writeUTF(delta.stableType());
                out.writeInt(delta.slots());
            }
            out.writeBoolean(value.pendingCreation().isPresent());
            if (value.pendingCreation().isPresent()) {
                ZLinkPendingObjectCreation pending =
                    value.pendingCreation().orElseThrow();
                out.writeUTF(pending.reservationId());
                out.writeUTF(pending.requestContentReference());
                writeBytes(out, pending.requestSha256());
                out.writeInt(pending.requestEncodedSize());
            }
            out.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new IllegalStateException(failure);
        }
    }

    private static AuthorityRecord decode(byte[] bytes) {
        try {
            var in = new DataInputStream(new ByteArrayInputStream(bytes));
            if (in.readInt() != CODEC_VERSION) {
                throw new IOException("unsupported version");
            }
            byte[] payload = readBytes(in);
            long objectGeneration = in.readLong();
            long ownerGeneration = in.readLong();
            String ownerId = in.readUTF();
            long ownerLeaseGeneration = in.readLong();
            var state = ZLinkPlacementAllocationState.values()[in.readInt()];
            var kind = ZLinkPlacementObjectKind.values()[in.readInt()];
            String stableType = in.readUTF();
            var descriptor = new ZLinkMeshNodeDescriptorKey(
                in.readUTF(), RoutingId.fromHex(in.readUTF()));
            long descriptorGeneration = in.readLong();
            int actorSlots = in.readInt();
            int spotSlots = in.readInt();
            Optional<ZLinkSpotTypeCapacityDelta> spotType =
                in.readBoolean()
                    ? Optional.of(new ZLinkSpotTypeCapacityDelta(
                        ZLinkPlacementObjectKind.values()[in.readInt()],
                        in.readUTF(),
                        in.readInt()))
                    : Optional.empty();
            Optional<ZLinkPendingObjectCreation> pending =
                in.readBoolean()
                    ? Optional.of(new ZLinkPendingObjectCreation(
                        in.readUTF(),
                        in.readUTF(),
                        readBytes(in),
                        in.readInt()))
                    : Optional.empty();
            if (in.available() != 0) {
                throw new IOException("trailing bytes");
            }
            return new AuthorityRecord(
                payload,
                objectGeneration,
                ownerGeneration,
                ownerId,
                ownerLeaseGeneration,
                new ZLinkPlacementAllocation(
                    state,
                    kind,
                    stableType,
                    descriptor,
                    descriptorGeneration,
                    new ZLinkPlacementCapacityBundle(
                        actorSlots, spotSlots, spotType)),
                pending);
        } catch (IOException | RuntimeException failure) {
            throw new IllegalStateException(
                "Location Store authority record is invalid",
                failure);
        }
    }

    private static byte[] encodeAggregate(
        byte state,
        ZLinkAggregatePrepareRequest request) {
        try {
            var bytes = new ByteArrayOutputStream();
            var out = new DataOutputStream(bytes);
            out.writeByte(state);
            out.writeLong(request.aggregateId().getMostSignificantBits());
            out.writeLong(request.aggregateId().getLeastSignificantBits());
            out.writeLong(request.aggregateGeneration());
            out.writeUTF(request.targetDescriptor().meshName());
            out.writeUTF(request.targetDescriptor().rid().toHex());
            out.writeLong(request.targetDescriptorLifecycleGeneration());
            out.writeUTF(request.targetOwner().ownerId());
            out.writeLong(request.targetOwner().leaseGeneration());
            writeBytes(out, request.inventoryDigest());
            out.writeInt(request.capacityBundle().actorSlots());
            out.writeInt(request.capacityBundle().spotSlots());
            out.writeBoolean(request.capacityBundle().spotType().isPresent());
            if (request.capacityBundle().spotType().isPresent()) {
                ZLinkSpotTypeCapacityDelta delta =
                    request.capacityBundle().spotType().orElseThrow();
                out.writeInt(delta.objectKind().ordinal());
                out.writeUTF(delta.stableType());
                out.writeInt(delta.slots());
            }
            out.writeInt(request.participants().size());
            for (ZLinkAggregateParticipant participant
                : request.participants()) {
                out.writeUTF(participant.authorityKey());
                out.writeUTF(participant.expectedStoreVersion());
                out.writeInt(participant.ownerTransition().ordinal());
                writeBytes(out, participant.authorityPayload());
                writeBytes(out, participant.membershipMutation());
            }
            out.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new IllegalStateException(failure);
        }
    }

    private static PreparedAggregate decodeAggregate(byte[] bytes) {
        try {
            var in = new DataInputStream(new ByteArrayInputStream(bytes));
            byte state = in.readByte();
            var id = new java.util.UUID(in.readLong(), in.readLong());
            long generation = in.readLong();
            var descriptor = new ZLinkMeshNodeDescriptorKey(
                in.readUTF(), RoutingId.fromHex(in.readUTF()));
            long descriptorGeneration = in.readLong();
            var owner = new ZLinkLocationOwnerToken(
                in.readUTF(), in.readLong());
            byte[] digest = readBytes(in);
            int actors = in.readInt();
            int spots = in.readInt();
            Optional<ZLinkSpotTypeCapacityDelta> spotType =
                in.readBoolean()
                    ? Optional.of(new ZLinkSpotTypeCapacityDelta(
                        ZLinkPlacementObjectKind.values()[in.readInt()],
                        in.readUTF(),
                        in.readInt()))
                    : Optional.empty();
            int count = in.readInt();
            List<ZLinkAggregateParticipant> participants =
                new ArrayList<>(count);
            for (int index = 0; index < count; index++) {
                participants.add(new ZLinkAggregateParticipant(
                    in.readUTF(),
                    in.readUTF(),
                    ZLinkAuthorityGenerationTransition.values()[
                        in.readInt()],
                    readBytes(in),
                    readBytes(in)));
            }
            return new PreparedAggregate(
                state,
                new ZLinkAggregatePrepareRequest(
                    id,
                    generation,
                    participants,
                    digest,
                    descriptor,
                    descriptorGeneration,
                    new ZLinkPlacementCapacityBundle(
                        actors, spots, spotType),
                    owner));
        } catch (IOException | RuntimeException failure) {
            throw new IllegalStateException(
                "Location Store aggregate record is invalid",
                failure);
        }
    }

    private static void writeBytes(DataOutputStream out, byte[] bytes)
        throws IOException {
        out.writeInt(bytes.length);
        out.write(bytes);
    }

    private static byte[] readBytes(DataInputStream in) throws IOException {
        int length = in.readInt();
        if (length < 0 || length > 64 * 1024 * 1024) {
            throw new IOException("invalid byte length");
        }
        return in.readNBytes(length);
    }

    private static ZLinkStoreKey authorityKey(String key) {
        return new ZLinkStoreKey(
            PREFIX + HexFormat.of().formatHex(
                key.getBytes(StandardCharsets.UTF_8)));
    }

    private static String decodeAuthorityKey(ZLinkStoreKey key) {
        return new String(
            HexFormat.of().parseHex(key.value().substring(PREFIX.length())),
            StandardCharsets.UTF_8);
    }

    private static ZLinkStoreKey ownerKey(String ownerId) {
        byte[] bytes = ownerId.getBytes(StandardCharsets.UTF_8);
        return new ZLinkStoreKey(
            "zlink:v11:owner:" + bytes.length + ":" + ownerId + ":");
    }

    private static ZLinkStoreKey relocationKey(
        ZLinkRelocationCapacityFence fence) {
        return new ZLinkStoreKey(
            "zlink:v11:relocation-capacity:" + fence.value());
    }

    private static ZLinkStoreKey aggregateKey(ZLinkAggregateFence fence) {
        return new ZLinkStoreKey(
            "zlink:v11:aggregate:" + fence.aggregateId()
                + ":" + fence.aggregateGeneration());
    }

    private static long ownerGeneration(byte[] bytes) {
        try {
            var in = new DataInputStream(new ByteArrayInputStream(bytes));
            int length = in.readInt();
            if (length < 1 || length > bytes.length - 12) {
                throw new IOException();
            }
            in.skipNBytes(length);
            return in.readLong();
        } catch (IOException failure) {
            throw new IllegalStateException(
                "Location Store owner record is invalid", failure);
        }
    }

    private static byte[] encodeLong(long value) {
        return java.nio.ByteBuffer.allocate(Long.BYTES)
            .putLong(value).array();
    }

    private static long decodeLong(byte[] bytes) {
        if (bytes.length != Long.BYTES) {
            throw new IllegalStateException(
                "Location Store counter is invalid");
        }
        return java.nio.ByteBuffer.wrap(bytes).getLong();
    }

    private static systems.zlink.framework.locationprovider
        .ZLinkStoreCancellation adapt(ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(cancellation, "cancellation");
        return cancellation::isCancellationRequested;
    }

    private static void requireKey(String key) {
        if (key == null || key.isBlank()) {
            throw new IllegalArgumentException(
                "authority key must not be blank");
        }
    }

    private static <T> CompletionStage<T> completed(T value) {
        return CompletableFuture.completedFuture(value);
    }

    private static List<ZLinkStoreMutation> concat(
        List<ZLinkStoreMutation> left,
        List<ZLinkStoreMutation> right) {
        List<ZLinkStoreMutation> result = new ArrayList<>(left);
        result.addAll(right);
        return result;
    }

    private record AuthorityRecord(
        byte[] payload,
        long objectGeneration,
        long authorityOwnerGeneration,
        String ownerId,
        long ownerLeaseGeneration,
        ZLinkPlacementAllocation allocation,
        Optional<ZLinkPendingObjectCreation> pendingCreation) {
        AuthorityRecord {
            payload = payload.clone();
        }

        AuthorityRecord withPayload(byte[] next) {
            return new AuthorityRecord(
                next,
                objectGeneration,
                authorityOwnerGeneration,
                ownerId,
                ownerLeaseGeneration,
                allocation,
                pendingCreation);
        }

        AuthorityRecord withOwner(
            byte[] next,
            long generation,
            ZLinkLocationOwnerToken owner) {
            return new AuthorityRecord(
                next,
                objectGeneration,
                generation,
                owner.ownerId(),
                owner.leaseGeneration(),
                allocation,
                pendingCreation);
        }
    }

    private record Counter(long value, List<ZLinkStoreMutation> mutations) {}
    private record Counters(
        long objectGeneration,
        long ownerGeneration,
        List<ZLinkStoreMutation> mutations) {}
    private record DecodedItem(String key, ZLinkStoreValue value) {}
    private record LoadedParticipant(
        ZLinkStoreKey key,
        ZLinkStoreValue value) {}
    private record PreparedAggregate(
        byte state,
        ZLinkAggregatePrepareRequest request) {}
}
