using System.Collections.Concurrent;
using System.Security.Cryptography;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Service;

/// <summary>
/// Owns the target-side command 40/30 reservation handshake. The offer is a
/// side-effect-free statement of the configured local admission budget. The
/// exact source acceptance is the only transition that acquires runtime and
/// Location Store capacity, and staging cannot overtake that transition.
/// </summary>
internal sealed class ZLinkCanonicalRelocationReservationOwner
    : ICanonicalRelocationReservationTarget, IAsyncDisposable
{
    private const int DefaultMaximumSlots = 1_024;
    private const int MaximumTerminalReservations = 1_024;
    private static readonly TimeSpan TerminalRetention = TimeSpan.FromMinutes(5);
    private readonly IZLinkLocationRepository _store;
    private readonly IZLinkRelocationRepository? _relocationStore;
    private readonly ZLinkSpotRetireTargetRuntime? _targetRuntime;
    private readonly ZLinkStandaloneActorRelocationRuntime?
        _standaloneActorRuntime;
    private readonly ZLinkRelocationPermitPool _permits;
    private readonly string _meshName;
    private readonly RoutingId _localNodeRid;
    private readonly ulong _localNodeGeneration;
    private readonly TimeProvider _timeProvider;
    private readonly TimeSpan _acceptDeadline;
    private readonly int _maximumSlots;
    private readonly ConcurrentDictionary<ReservationKey, ReservationSlot> _slots = new();
    private readonly ConcurrentDictionary<ReservationKey, ReservationTerminal>
        _terminals = new();
    private readonly ConcurrentQueue<ReservationKey> _terminalOrder = new();
    private int _activeSlots;
    private long _nextReservationGeneration;
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _sweepLoop;

    internal ZLinkCanonicalRelocationReservationOwner(
        IZLinkLocationRepository store,
        ZLinkRelocationPermitPool permits,
        string meshName,
        RoutingId localNodeRid,
        ulong localNodeGeneration,
        TimeSpan acceptDeadline,
        TimeProvider? timeProvider = null,
        int maximumSlots = DefaultMaximumSlots,
        IZLinkRelocationRepository? relocationStore = null,
        ZLinkSpotRetireTargetRuntime? targetRuntime = null,
        ZLinkStandaloneActorRelocationRuntime? standaloneActorRuntime = null)
    {
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _relocationStore = relocationStore;
        _targetRuntime = targetRuntime;
        _standaloneActorRuntime = standaloneActorRuntime;
        _permits = permits ?? throw new ArgumentNullException(nameof(permits));
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        if (localNodeRid.IsEmpty || localNodeGeneration == 0)
            throw new ArgumentOutOfRangeException(nameof(localNodeGeneration));
        if (acceptDeadline <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(acceptDeadline));
        if (maximumSlots <= 0)
            throw new ArgumentOutOfRangeException(nameof(maximumSlots));
        _meshName = meshName;
        _localNodeRid = localNodeRid;
        _localNodeGeneration = localNodeGeneration;
        _acceptDeadline = acceptDeadline;
        _timeProvider = timeProvider ?? TimeProvider.System;
        _maximumSlots = maximumSlots;
        _sweepLoop = Task.Run(SweepLoopAsync);
    }

    public async ValueTask<ZLinkServiceWireCodec.RelocationReadyRecord> OfferAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        ValidatePrepareRoute(prepare, authenticatedSourceNodeRid);
        var key = new ReservationKey(prepare.RelocationId,
            prepare.TargetAttemptGeneration);
        var fingerprint = SHA256.HashData(
            ZLinkServiceWireCodec.EncodeRelocationPrepare(prepare));
        CleanupTerminals();
        if (_terminals.TryGetValue(key, out var terminal))
        {
            if (!terminal.PrepareFingerprint.AsSpan().SequenceEqual(fingerprint)
                || terminal.AuthenticatedSourceNodeRid
                   != authenticatedSourceNodeRid)
                throw Conflict("A terminal command 40 retry changed fields.");
            return terminal.Offer;
        }
        ReservationSlot slot;
        var creator = false;
        while (true)
        {
            if (_slots.TryGetValue(key, out slot!))
            {
                if (!slot.Fingerprint.AsSpan().SequenceEqual(fingerprint)
                    || slot.AuthenticatedSourceNodeRid != authenticatedSourceNodeRid)
                    throw Conflict("A command 40 retry changed its authenticated fields.");
                break;
            }
            if (!TryAcquireSlot())
                throw new ZLinkFrameworkException(ZLinkFrameworkErrorKind.SpotMoving,
                    "The canonical relocation reservation table is full.", true);
            slot = new ReservationSlot(fingerprint, prepare,
                authenticatedSourceNodeRid);
            if (_slots.TryAdd(key, slot))
            {
                creator = true;
                break;
            }
            ReleaseSlot(slot);
        }

        if (creator)
        {
            try
            {
                if (!_permits.TryGetInboundOffer(
                        out var offeredMessages,
                        out var offeredBytes))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotMoving,
                        "The target cannot currently offer relocation capacity.",
                        true);
                var generation = checked((ulong)Interlocked.Increment(
                    ref _nextReservationGeneration));
                var progress = prepare.Participants.Select(static participant =>
                    new ZLinkServiceWireCodec.RelocationParticipantProgressRecord(
                        participant.ParticipantId, 0, 0)).ToArray();
                var offer = new ZLinkServiceWireCodec.RelocationReadyRecord(
                    prepare.RelocationId, prepare.TargetAttemptGeneration,
                    prepare.RoundKind, prepare.Coordinator, prepare.Candidate,
                    prepare.Object, 2, offeredMessages, offeredBytes, [],
                    prepare.SourceNodeGeneration, _localNodeGeneration, generation,
                    prepare.Root, prepare.ApplicationVersion, progress);
                lock (slot.Gate)
                {
                    slot.Offer = offer;
                    slot.ExpiresAt = _timeProvider.GetUtcNow() + _acceptDeadline;
                    slot.State = ReservationState.Offered;
                }
                slot.OfferCompletion.TrySetResult(offer);
            }
            catch (Exception exception)
            {
                TryRemoveSlot(key, slot);
                slot.OfferCompletion.TrySetException(exception);
                throw;
            }
        }
        return await slot.OfferCompletion.Task.WaitAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkServiceWireCodec.RelocationReservedRecord>
        AcceptAsync(
            ZLinkServiceWireCodec.RelocationReadyRecord acceptance,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
    {
        var key = new ReservationKey(acceptance.RelocationId,
            acceptance.TargetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
        {
            if (_terminals.TryGetValue(key, out var terminal)
                && terminal.AuthenticatedSourceNodeRid
                   == authenticatedSourceNodeRid
                && terminal.AcceptanceFingerprint.AsSpan().SequenceEqual(
                    SHA256.HashData(
                        ZLinkServiceWireCodec.EncodeRelocationReady(acceptance))))
                return terminal.Reserved;
            throw Conflict("Command 30 has no matching command 40 slot.");
        }
        _ = await slot.OfferCompletion.Task.WaitAsync(cancellationToken)
            .ConfigureAwait(false);

        Task<ZLinkServiceWireCodec.RelocationReservedRecord> operation;
        lock (slot.Gate)
        {
            if (slot.State is ReservationState.Accepted or ReservationState.Staged)
            {
                if (!MatchesAcceptance(slot, acceptance,
                        authenticatedSourceNodeRid))
                    throw Conflict("A command 30 retry changed accepted fields.");
                return slot.Reserved!;
            }
            if (slot.State == ReservationState.Accepting)
            {
                if (!MatchesAcceptance(slot, acceptance,
                        authenticatedSourceNodeRid))
                    throw Conflict("An in-flight command 30 retry changed fields.");
                operation = slot.AcceptanceOperation!;
            }
            else if (slot.State != ReservationState.Offered)
                throw Conflict("The relocation reservation is not offered.");
            else if (_timeProvider.GetUtcNow() >= slot.ExpiresAt)
            {
                slot.State = ReservationState.Expired;
                TryRemoveSlot(key, slot);
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.DeadlineExceeded,
                    "The local relocation acceptance deadline elapsed.", true);
            }
            else
            {
                if (!MatchesAcceptance(slot, acceptance,
                        authenticatedSourceNodeRid))
                    throw Conflict("Command 30 is not an exact source acceptance.");
                slot.State = ReservationState.Accepting;
                operation = CompleteAcceptanceAsync(slot, key, acceptance);
                slot.AcceptanceOperation = operation;
            }
        }
        return await operation.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task<ZLinkServiceWireCodec.RelocationReservedRecord>
        CompleteAcceptanceAsync(
            ReservationSlot slot,
            ReservationKey key,
            ZLinkServiceWireCodec.RelocationReadyRecord acceptance)
    {
        using var acceptanceDeadline = CancellationTokenSource
            .CreateLinkedTokenSource(_stop.Token);
        acceptanceDeadline.CancelAfter(_acceptDeadline);
        var acceptanceToken = acceptanceDeadline.Token;
        if (!_permits.TryAcquire(
                ZLinkRelocationPermitRequest.Inbound(
                    checked((long)slot.Prepare.RequiredBytes),
                    restore: true),
                out var permit))
        {
            var exception = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                "The offered runtime capacity is no longer available.", true);
            ResetAcceptance(slot);
            throw exception;
        }
        ZLinkPreparedAggregateRelocation? prepared = null;
        ZLinkRelocationCapacityFence? capacityFence = null;
        IReadOnlyDictionary<ulong, IReadOnlyList<byte[]>>? expectedRecords = null;
        try
        {
            if (ObjectKind(slot.Prepare.Object.Kind)
                == ZLinkPlacementObjectKind.UserSpot)
                prepared = await PrepareAggregateAsync(
                        slot.Prepare, acceptanceToken)
                    .ConfigureAwait(false);
            else
            {
                capacityFence = await ReserveCapacityAsync(
                        slot.Prepare, key, acceptanceToken)
                    .ConfigureAwait(false);
                if (slot.Prepare.Root is not null
                    && _relocationStore is not null)
                    expectedRecords = await ReadExpectedRecordsAsync(
                            slot.Prepare, acceptanceToken)
                        .ConfigureAwait(false);
                if (ObjectKind(slot.Prepare.Object.Kind)
                        == ZLinkPlacementObjectKind.Actor
                    && _standaloneActorRuntime is not null)
                {
                    await _standaloneActorRuntime.StageTargetAsync(
                            slot.Prepare,
                            slot.AuthenticatedSourceNodeRid,
                            acceptanceToken)
                        .ConfigureAwait(false);
                }
            }
            if (ObjectKind(slot.Prepare.Object.Kind)
                == ZLinkPlacementObjectKind.UserSpot)
                expectedRecords = await ReadExpectedRecordsAsync(
                        slot.Prepare, acceptanceToken)
                    .ConfigureAwait(false);
            var reserved = new ZLinkServiceWireCodec.RelocationReservedRecord(
                acceptance.RelocationId, acceptance.TargetAttemptGeneration,
                acceptance.RoundKind, acceptance.Coordinator,
                acceptance.Candidate, acceptance.ReservationGeneration,
                acceptance.Participants);
            lock (slot.Gate)
            {
                if (slot.State != ReservationState.Accepting)
                    throw Conflict("The command 30 transition lost its slot.");
                slot.PreparedAggregate = prepared;
                slot.CapacityFence = capacityFence;
                slot.ExpectedRecords = expectedRecords;
                slot.Permit = permit;
                slot.Acceptance = acceptance;
                slot.Reserved = reserved;
                slot.ExpiresAt = _timeProvider.GetUtcNow() + _acceptDeadline;
                slot.State = ReservationState.Accepted;
            }
            permit = default;
            return reserved;
        }
        catch
        {
            try
            {
                if (slot.Prepare.Object.Kind == 1
                    && _standaloneActorRuntime is not null)
                    await _standaloneActorRuntime.AbortTargetAsync(
                            slot.Prepare)
                        .ConfigureAwait(false);
                if (capacityFence is { } capacity)
                    _ = await _store.AbortRelocationCapacityAsync(
                            capacity, CancellationToken.None)
                        .ConfigureAwait(false);
                if (prepared is not null)
                    await new ZLinkAggregateRelocationCoordinator(
                            _store, _relocationStore!)
                        .AbortAsync(prepared)
                        .ConfigureAwait(false);
            }
            finally
            {
                ResetAcceptance(slot);
            }
            throw;
        }
        finally
        {
            permit.Dispose();
        }
    }

    private static void ResetAcceptance(ReservationSlot slot)
    {
        lock (slot.Gate)
        {
            if (slot.State == ReservationState.Accepting)
                slot.State = ReservationState.Offered;
            slot.AcceptanceOperation = null;
        }
    }

    private void RememberTerminal(
        ReservationKey key,
        ReservationSlot slot,
        ZLinkServiceWireCodec.RelocationSealRecord seal)
    {
        var digests = slot.Records.ToDictionary(
            static participant => participant.Key,
            static participant => (IReadOnlyDictionary<ulong, byte[]>)
                participant.Value.ToDictionary(
                    static record => record.Key,
                    static record => SHA256.HashData(record.Value)));
        var terminal = new ReservationTerminal(
            slot.Fingerprint,
            slot.Prepare,
            slot.AuthenticatedSourceNodeRid,
            slot.Offer ?? throw Conflict("Terminal relocation lost its offer."),
            SHA256.HashData(ZLinkServiceWireCodec.EncodeRelocationReady(
                slot.Acceptance
                ?? throw Conflict("Terminal relocation lost its acceptance."))),
            slot.Reserved
            ?? throw Conflict("Terminal relocation lost its reservation."),
            SHA256.HashData(ZLinkServiceWireCodec.EncodeRelocationSeal(seal)),
            digests,
            _timeProvider.GetUtcNow() + TerminalRetention);
        _terminals[key] = terminal;
        _terminalOrder.Enqueue(key);
        CleanupTerminals();
    }

    private void CleanupTerminals()
    {
        var now = _timeProvider.GetUtcNow();
        while (_terminalOrder.TryPeek(out var key))
        {
            _terminals.TryGetValue(key, out var terminal);
            if (_terminalOrder.Count <= MaximumTerminalReservations
                && terminal is not null
                && terminal.ExpiresAt > now)
                break;
            _terminalOrder.TryDequeue(out _);
            if (terminal is not null)
                _terminals.TryRemove(
                    new KeyValuePair<ReservationKey, ReservationTerminal>(
                        key, terminal));
        }
    }

    private bool TryAcquireSlot()
    {
        while (true)
        {
            var current = Volatile.Read(ref _activeSlots);
            if (current >= _maximumSlots) return false;
            if (Interlocked.CompareExchange(
                    ref _activeSlots, current + 1, current) == current)
                return true;
        }
    }

    private void ReleaseSlot(ReservationSlot slot)
    {
        if (Interlocked.Exchange(ref slot.CapacityReleased, 1) == 0)
            Interlocked.Decrement(ref _activeSlots);
    }

    private bool TryRemoveSlot(ReservationKey key, ReservationSlot slot)
    {
        if (!_slots.TryRemove(
                new KeyValuePair<ReservationKey, ReservationSlot>(key, slot)))
            return false;
        ReleaseSlot(slot);
        return true;
    }

    internal bool CompleteSuccessfulStaging(
        ZLinkServiceWireCodec.RelocationWireId relocationId,
        ulong targetAttemptGeneration)
    {
        var key = new ReservationKey(relocationId, targetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot)) return false;
        lock (slot.Gate)
        {
            if (slot.State != ReservationState.Staged)
                throw Conflict(
                    "A canonical reservation can complete only after staging.");
            return CompleteSuccessfulSlot(key, slot);
        }
    }

    private bool CompleteSuccessfulSlot(
        ReservationKey key,
        ReservationSlot slot)
    {
        ZLinkRelocationPermitPool.ZLinkRelocationPermitLease permit;
        lock (slot.Gate)
        {
            if (!TryRemoveSlot(key, slot)) return false;
            permit = slot.Permit;
            slot.Permit = default;
        }
        if (slot.Prepare.Object.Kind == 1
            && _standaloneActorRuntime is not null)
        {
            try
            {
                _standaloneActorRuntime.RetainTargetPermit(
                    slot.Prepare,
                    permit);
            }
            catch
            {
                permit.Dispose();
                throw;
            }
        }
        else
        {
            permit.Dispose();
        }
        return true;
    }

    private async ValueTask CleanupFailedSlotAsync(
        ReservationKey key,
        ReservationSlot slot)
    {
        ZLinkPreparedAggregateRelocation? prepared;
        ZLinkRelocationCapacityFence? capacity;
        ZLinkRelocationPermitPool.ZLinkRelocationPermitLease permit;
        lock (slot.Gate)
        {
            prepared = slot.PreparedAggregate;
            capacity = slot.CapacityFence;
            permit = slot.Permit;
            slot.PreparedAggregate = null;
            slot.CapacityFence = null;
            slot.Permit = default;
            slot.State = ReservationState.Expired;
        }
        if (!TryRemoveSlot(key, slot)) return;
        await CleanupRemovedSlotResourcesAsync(
                slot, prepared, capacity, permit)
            .ConfigureAwait(false);
    }

    private async ValueTask CleanupRemovedSlotResourcesAsync(
        ReservationSlot slot,
        ZLinkPreparedAggregateRelocation? prepared,
        ZLinkRelocationCapacityFence? capacity,
        ZLinkRelocationPermitPool.ZLinkRelocationPermitLease permit)
    {
        List<Exception>? failures = null;
        if (slot.Prepare.Object.Kind == 1
            && _standaloneActorRuntime is not null)
        {
            try
            {
                await _standaloneActorRuntime.AbortTargetAsync(slot.Prepare)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        try
        {
            permit.Dispose();
        }
        catch (Exception exception)
        {
            (failures ??= []).Add(exception);
        }

        if (capacity is { } exactCapacity)
        {
            try
            {
                _ = await _store.AbortRelocationCapacityAsync(
                        exactCapacity, CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        if (prepared is not null)
        {
            try
            {
                await new ZLinkAggregateRelocationCoordinator(
                        _store, _relocationStore!)
                    .AbortAsync(prepared).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        if (failures is not null)
            throw new AggregateException(
                "Canonical relocation reservation cleanup failed.",
                failures);
    }

    internal void BeginStaging(
        ZLinkServiceWireCodec.RelocationWireId relocationId,
        ulong targetAttemptGeneration)
    {
        var key = new ReservationKey(relocationId, targetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
            throw Conflict("Staging has no canonical reservation slot.");
        lock (slot.Gate)
        {
            if (slot.State is ReservationState.Staged
                or ReservationState.StagingActive) return;
            if (slot.State != ReservationState.Accepted)
                throw Conflict("Staging cannot overtake command 30 acceptance.");
            slot.State = ReservationState.Staged;
        }
    }

    public async ValueTask<ZLinkServiceWireCodec.RelocationAckRecord> StageDataAsync(
        ZLinkServiceWireCodec.RelocationDataRecord data,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var key = new ReservationKey(data.RelocationId,
            data.TargetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
        {
            if (_terminals.TryGetValue(key, out var terminal))
                return terminal.ReplayAck(data, authenticatedSourceNodeRid);
            throw Conflict("Command 31 has no matching reservation.");
        }
        if (slot.Prepare.Object.Kind == 1)
            await RefreshStandaloneFinalRootAsync(slot, cancellationToken)
                .ConfigureAwait(false);
        lock (slot.Gate)
        {
            ValidateAttempt(slot, data.Coordinator,
                authenticatedSourceNodeRid, data.SenderRole);
            if (slot.State is not (ReservationState.Accepted
                    or ReservationState.Streaming))
                throw Conflict("Command 31 cannot overtake command 30.");
            var allowance = slot.Prepare.Participants.SingleOrDefault(
                participant => participant.ParticipantId == data.ParticipantId)
                ?? throw Conflict("Command 31 participant is not negotiated.");
            if (data.Sequence == 0
                || data.Sequence > allowance.AllowanceMessages)
                throw Conflict("Command 31 sequence exceeds its allowance.");
            if (!slot.Records.TryGetValue(data.ParticipantId, out var records))
            {
                records = new SortedDictionary<ulong, byte[]>();
                slot.Records.Add(data.ParticipantId, records);
            }
            var encoded = data.FrozenRecord.Encoded.ToArray();
            if (slot.ExpectedRecords is null
                || !slot.ExpectedRecords.TryGetValue(data.ParticipantId,
                    out var expected)
                || data.Sequence > checked((ulong)expected.Count)
                || !expected[checked((int)data.Sequence - 1)].AsSpan()
                    .SequenceEqual(encoded))
                throw Conflict(
                    "Command 31 does not match the immutable root.");
            if (records.TryGetValue(data.Sequence, out var duplicate))
            {
                if (!duplicate.AsSpan().SequenceEqual(encoded))
                    throw Conflict(
                        "A command 31 retry changed frozen record bytes.");
            }
            else
            {
                var bytes = checked(slot.RecordBytes.GetValueOrDefault(
                    data.ParticipantId) + (ulong)encoded.Length);
                if (bytes > allowance.AllowanceBytes)
                    throw Conflict("Command 31 bytes exceed their allowance.");
                records.Add(data.Sequence, encoded);
                slot.RecordBytes[data.ParticipantId] = bytes;
            }
            slot.State = ReservationState.Streaming;
            ulong highWater = 0;
            while (records.ContainsKey(highWater + 1)) highWater++;
            return new ZLinkServiceWireCodec.RelocationAckRecord(
                data.RelocationId, data.TargetAttemptGeneration,
                data.Coordinator, 2, data.ParticipantId, highWater);
        }
    }

    public bool TryCreateSealRequest(
        ZLinkServiceWireCodec.RelocationWireId relocationId,
        ulong targetAttemptGeneration,
        out ZLinkServiceWireCodec.RelocationSealRecord seal)
    {
        var key = new ReservationKey(relocationId, targetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
        {
            seal = null!;
            return false;
        }
        lock (slot.Gate)
        {
            if (slot.Prepare.Object.Kind == 1)
            {
                seal = null!;
                return false;
            }
            if (slot.SealRequested
                || slot.State is not (ReservationState.Accepted
                    or ReservationState.Streaming))
            {
                seal = null!;
                return false;
            }
            foreach (var participant in slot.Prepare.Participants)
                if ((!slot.Records.TryGetValue(participant.ParticipantId,
                         out var records)
                     && participant.AllowanceMessages != 0)
                    || (records?.Count ?? 0)
                       != checked((int)participant.AllowanceMessages)
                    || slot.RecordBytes.GetValueOrDefault(
                        participant.ParticipantId) > participant.AllowanceBytes)
                {
                    seal = null!;
                    return false;
                }
            slot.SealRequested = true;
            seal = new ZLinkServiceWireCodec.RelocationSealRecord(
                relocationId, targetAttemptGeneration,
                slot.Prepare.Coordinator, 2, false, []);
            return true;
        }
    }

    public async ValueTask AcceptSealResponseAsync(
        ZLinkServiceWireCodec.RelocationSealRecord seal,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        var key = new ReservationKey(seal.RelocationId,
            seal.TargetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
        {
            if (_terminals.TryGetValue(key, out var terminal))
            {
                terminal.ValidateSeal(seal, authenticatedSourceNodeRid);
                if (terminal.Prepare.Object.Kind == 1
                    && _standaloneActorRuntime is not null)
                    await _standaloneActorRuntime.ReconcilePublishedTargetAsync(
                            terminal.Prepare,
                            cancellationToken)
                        .ConfigureAwait(false);
                return;
            }
            throw Conflict("Command 34 has no matching reservation.");
        }
        if (slot.Prepare.Object.Kind == 1)
            await RefreshStandaloneFinalRootAsync(slot, cancellationToken)
                .ConfigureAwait(false);
        lock (slot.Gate)
        {
            ValidateAttempt(slot, seal.Coordinator,
                authenticatedSourceNodeRid, seal.SenderRole);
            if (!seal.Response
                || slot.Prepare.Object.Kind != 1 && !slot.SealRequested
                || seal.Participants.Count != slot.Prepare.Participants.Count)
                throw Conflict("Command 34 response is not exact.");
            var expected = slot.Prepare.Participants
                .OrderBy(static participant => participant.ParticipantId)
                .ToArray();
            var actual = seal.Participants
                .OrderBy(static participant => participant.ParticipantId)
                .ToArray();
            for (var index = 0; index < expected.Length; index++)
            {
                var expectedHighWater = slot.Prepare.Object.Kind == 1
                    ? checked((ulong)(slot.ExpectedRecords?
                        .GetValueOrDefault(expected[index].ParticipantId)
                        ?.Count ?? -1))
                    : expected[index].AllowanceMessages;
                if (actual[index].ParticipantId != expected[index].ParticipantId
                    || actual[index].HighWater != expectedHighWater)
                    throw Conflict("Command 34 high-water is not exact.");
            }
            if (slot.State == ReservationState.StagingActive) return;
            if (slot.State is not (ReservationState.Accepted
                    or ReservationState.Streaming))
                throw Conflict("Command 34 cannot overtake command 31.");
            slot.State = ReservationState.Staged;
        }
        try
        {
            if (slot.Prepare.Object.Kind == 1)
            {
                if (_standaloneActorRuntime is null)
                    throw Conflict(
                        "Standalone Actor target materialization is not configured.");
                await PublishStandaloneObjectAsync(slot, cancellationToken)
                    .ConfigureAwait(false);
                _standaloneActorRuntime.MarkAuthorityPublished(slot.Prepare);
                await _standaloneActorRuntime.ActivatePublishedTargetAsync(
                        slot.Prepare,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            else
            {
                if (_targetRuntime is null)
                    throw Conflict(
                        "Canonical target materialization is not configured.");
                await _targetRuntime.StageCanonicalInboundAsync(
                        slot.Prepare, authenticatedSourceNodeRid, cancellationToken)
                    .ConfigureAwait(false);
                if (slot.Prepare.Object.Kind == 3)
                    await PublishStandaloneObjectAsync(slot, cancellationToken)
                        .ConfigureAwait(false);
                await _targetRuntime.PublishCanonicalInboundAsync(
                        slot.Prepare, authenticatedSourceNodeRid, cancellationToken)
                    .ConfigureAwait(false);
            }
            RememberTerminal(key, slot, seal);
            CompleteSuccessfulSlot(key, slot);
        }
        catch
        {
            if (slot.Prepare.Object.Kind == 1
                && _standaloneActorRuntime is not null
                && _standaloneActorRuntime.IsAuthorityPublished(slot.Prepare))
            {
                RememberTerminal(key, slot, seal);
                CompleteSuccessfulSlot(key, slot);
                throw;
            }
            if (slot.Prepare.Object.Kind == 1
                && _standaloneActorRuntime is not null)
                await _standaloneActorRuntime.AbortTargetAsync(slot.Prepare)
                    .ConfigureAwait(false);
            await CleanupFailedSlotAsync(key, slot).ConfigureAwait(false);
            throw;
        }
    }

    public async ValueTask CompleteAsync(
        ZLinkServiceWireCodec.RelocationCompleteRecord complete,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (complete.SenderRole != 1
            || complete.Coordinator.NodeRid != authenticatedSourceNodeRid
            || complete.SourceCleanupState is 0 or > 2)
            throw Conflict("Command 35 source cleanup fence is invalid.");
        var key = new ReservationKey(complete.RelocationId,
            complete.TargetAttemptGeneration);
        if (_slots.ContainsKey(key))
            throw Conflict("Command 35 cannot overtake target publication.");
        if (!_terminals.TryGetValue(key, out var terminal))
            throw Conflict("Command 35 has no completed relocation.");
        terminal.AcceptComplete(complete, authenticatedSourceNodeRid);
        if (terminal.Prepare.Object.Kind == 1)
        {
            if (_standaloneActorRuntime is null)
                throw Conflict(
                    "Standalone Actor target completion is not configured.");
            await _standaloneActorRuntime.CompleteTargetAsync(
                    complete,
                    authenticatedSourceNodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static void ValidateAttempt(
        ReservationSlot slot,
        ZLinkServiceWireCodec.RelocationCoordinatorFence coordinator,
        RoutingId authenticatedSourceNodeRid,
        byte senderRole)
    {
        if (senderRole != 1
            || coordinator != slot.Prepare.Coordinator
            || authenticatedSourceNodeRid != slot.AuthenticatedSourceNodeRid
            || coordinator.NodeRid != authenticatedSourceNodeRid
            || coordinator.NodeGeneration != slot.Prepare.SourceNodeGeneration)
            throw Conflict("Canonical relocation attempt fence changed.");
    }

    private async ValueTask PublishStandaloneObjectAsync(
        ReservationSlot slot,
        CancellationToken cancellationToken)
    {
        var capacity = slot.CapacityFence
            ?? throw Conflict(
                "Standalone relocation lost its capacity fence.");
        var root = (slot.Prepare.Object.Kind == 1
                ? slot.FinalRoot
                : slot.Prepare.Root)
            ?? throw Conflict(
                "Standalone relocation requires a root.");
        var relocationStore = _relocationStore
            ?? throw Conflict("Relocation Store is not configured.");
        var envelope = slot.Prepare.Object.Kind == 1
                       && slot.FinalEnvelope is { } finalEnvelope
            ? finalEnvelope
            : (await ZLinkRelocationTreeStore.ReadAsync(
                    relocationStore, root.Reference, root.ChecksumCrc32c,
                    cancellationToken)
                .ConfigureAwait(false)).Envelope;
        var participant = envelope.Participants.Single();
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            participant.RecoveryPayload.Span);
        if (slot.Prepare.Object.Kind == 1)
        {
            var captured = slot.FinalAuthority
                           ?? throw Conflict(
                               "Standalone Actor final authority is unavailable.");
            if (!ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                    recovery.AuthorityPayload.Span,
                    out var relocating)
                || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                    relocating.ApplicationPayload.Span,
                    out var targetAuthority))
                throw Conflict(
                    "Standalone Actor prepared publication payload is invalid.");
            await _standaloneActorRuntime!.ApplyFinalRootAsync(
                    slot.Prepare,
                    envelope,
                    cancellationToken)
                .ConfigureAwait(false);
            var reservationGeneration = slot.Acceptance?.ReservationGeneration
                                        ?? throw Conflict(
                                            "Standalone Actor prepared publication lost its reservation generation.");
            var targetOwner = new ZLinkLocationOwnerToken(
                slot.Prepare.Candidate.OwnerId,
                checked((long)slot.Prepare.Candidate.OwnerLeaseGeneration));
            var precommit = new ZLinkStandaloneActorRelocationPrecommitCoordinator(
                _store);
            var prepared = await precommit.PrepareTargetAsync(
                    captured,
                    envelope,
                    capacity,
                    slot.Prepare,
                    reservationGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
            _ = await precommit.CommitTargetAsync(
                    prepared,
                    envelope,
                    capacity,
                    targetAuthority,
                    targetOwner,
                    cancellationToken)
                .ConfigureAwait(false);
            slot.CapacityFence = null;
            return;
        }
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            _store, relocationStore);
        _ = await coordinator.PublishPreparedAsync(
                new ZLinkRelocationPublicationRequest(
                    recovery.AuthorityKey,
                    recovery.ExpectedStoreVersion,
                    ZLinkAuthorityGenerationTransition.NewOwner,
                    slot.Prepare.Candidate.OwnerId,
                    checked((long)slot.Prepare.Candidate
                        .OwnerLeaseGeneration),
                    recovery.AuthorityPayload,
                    capacity,
                    envelope),
                new ZLinkPreparedRelocation(
                    new ZLinkRelocationStored(
                        root.Reference,
                        root.ChecksumCrc32c,
                        _timeProvider.GetUtcNow() + TimeSpan.FromHours(24),
                        _timeProvider.GetUtcNow()),
                    envelope),
                cancellationToken)
            .ConfigureAwait(false);
        slot.CapacityFence = null;
    }

    private async ValueTask<IReadOnlyDictionary<ulong, IReadOnlyList<byte[]>>>
        ReadExpectedRecordsAsync(
            ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
            CancellationToken cancellationToken)
    {
        var root = prepare.Root
            ?? throw Conflict("Canonical relocation requires a root.");
        var relocationStore = _relocationStore
            ?? throw Conflict("Relocation Store is not configured.");
        var tree = await ZLinkRelocationTreeStore.ReadAsync(
                relocationStore, root.Reference, root.ChecksumCrc32c,
                cancellationToken)
            .ConfigureAwait(false);
        var objectKind = ObjectKind(prepare.Object.Kind);
        if (objectKind is ZLinkPlacementObjectKind.Actor
            or ZLinkPlacementObjectKind.InstanceSpot)
            ValidateStandaloneRoot(prepare, tree.Envelope, objectKind);
        var result = new Dictionary<ulong, IReadOnlyList<byte[]>>();
        foreach (var participant in tree.Envelope.Participants)
        {
            if (participant.CanonicalParticipantId == 0
                || !result.TryAdd(participant.CanonicalParticipantId,
                    participant.AcceptedJobs
                        .OrderBy(static job => job.AcceptedSequence)
                        .Select(static job => job.Payload.ToArray())
                        .ToArray()))
                throw Conflict(
                    "Immutable relocation root has invalid participant IDs.");
        }
        return result;
    }

    private async ValueTask RefreshStandaloneFinalRootAsync(
        ReservationSlot slot,
        CancellationToken cancellationToken)
    {
        lock (slot.Gate)
        {
            if (slot.FinalEnvelope is not null) return;
        }
        var prepare = slot.Prepare;
        var read = await _store.ReadAuthorityAsync(
                AuthorityKey(prepare.Object),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || StringComparer.Ordinal.Equals(
                found.Snapshot.StoreVersion,
                prepare.Coordinator.ExpectedAuthorityStoreVersion)
            || found.Snapshot.ObjectGeneration
               != prepare.Object.ObjectGeneration
            || found.Snapshot.AuthorityOwnerGeneration
               != prepare.Object.ExpectedAuthorityOwnerGeneration
            || !ZLinkCanonicalRelocationAuthorityStateCodec.TryRead(
                found.Snapshot.Payload.Span,
                out var projection)
            || projection.Phase != 2
            || projection.RelocationHigh != prepare.RelocationId.High
            || projection.RelocationLow != prepare.RelocationId.Low
            || projection.TargetAttemptGeneration != 0
            || string.IsNullOrWhiteSpace(projection.RelocationReference))
            throw Conflict(
                "Standalone Actor final root is not durably captured.");
        var relocationStore = _relocationStore
                              ?? throw Conflict(
                                  "Relocation Store is not configured.");
        var tree = await ZLinkRelocationTreeStore.ReadAsync(
                relocationStore,
                projection.RelocationReference,
                projection.RelocationChecksumCrc32c,
                cancellationToken)
            .ConfigureAwait(false);
        ValidateStandaloneRoot(
            prepare,
            tree.Envelope,
            ZLinkPlacementObjectKind.Actor);
        var finalRecords = tree.Envelope.Participants.Single()
            .AcceptedJobs
            .OrderBy(static job => job.AcceptedSequence)
            .Select(static job => job.Payload.ToArray())
            .ToArray();
        lock (slot.Gate)
        {
            if (slot.ExpectedRecords is null
                || !slot.ExpectedRecords.TryGetValue(1, out var initial)
                || initial.Count > finalRecords.Length)
                throw Conflict(
                    "Standalone Actor final root lost its initial journal prefix.");
            for (var index = 0; index < initial.Count; index++)
                if (!initial[index].AsSpan().SequenceEqual(finalRecords[index]))
                    throw Conflict(
                        "Standalone Actor final root changed its initial journal prefix.");
            if (slot.FinalEnvelope is not null)
            {
                if (slot.FinalRoot?.Reference != projection.RelocationReference
                    || slot.FinalRoot?.ChecksumCrc32c
                    != projection.RelocationChecksumCrc32c)
                    throw Conflict(
                        "Standalone Actor final root changed during staging.");
                return;
            }
            slot.ExpectedRecords = new Dictionary<ulong, IReadOnlyList<byte[]>>
            {
                [1] = finalRecords
            };
            slot.FinalEnvelope = tree.Envelope;
            slot.FinalRoot = new ZLinkServiceWireCodec.RelocationRootRecord(
                projection.RelocationReference,
                projection.RelocationChecksumCrc32c);
            slot.FinalAuthority = found.Snapshot;
        }
    }

    internal static void ValidateStandaloneRoot(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        ZLinkRelocationEnvelope envelope,
        ZLinkPlacementObjectKind expectedKind)
    {
        if (envelope.CanonicalLogicalStream.IsEmpty
            || envelope.CanonicalRelocationHigh != prepare.RelocationId.High
            || envelope.CanonicalRelocationLow != prepare.RelocationId.Low
            || envelope.CanonicalApplicationVersion < 0
            || checked((ulong)envelope.CanonicalApplicationVersion)
               != prepare.ApplicationVersion
            || envelope.Participants.Count != 1
            || prepare.Participants.Count != 1
            || prepare.RequiredBytes < checked((ulong)
                ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(envelope)))
            throw Conflict(
                "Command 40 does not match the exact standalone root.");
        var participant = envelope.Participants[0];
        var allowance = prepare.Participants[0];
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            participant.RecoveryPayload.Span);
        var acceptedBytes = participant.AcceptedJobs.Aggregate(
            0UL, static (sum, job) => checked(sum + (ulong)job.Payload.Length));
        if (participant.CanonicalParticipantId == 0
            || allowance.ParticipantId != participant.CanonicalParticipantId
            || allowance.AllowanceMessages
               < checked((ulong)participant.AcceptedJobs.Count)
            || allowance.AllowanceBytes < acceptedBytes
            || prepare.RequiredMessages != allowance.AllowanceMessages
            || allowance.AllowanceBytes > prepare.RequiredBytes
            || recovery.AuthorityKey != AuthorityKey(prepare.Object)
            || recovery.ObjectKind != expectedKind
            || participant.ObjectKind != recovery.ObjectKind
            || recovery.ObjectGeneration != prepare.Object.ObjectGeneration
            || participant.ObjectGeneration != recovery.ObjectGeneration
            || recovery.AuthorityOwnerGeneration
               != prepare.Object.ExpectedAuthorityOwnerGeneration
            || participant.AuthorityOwnerGeneration
               != recovery.AuthorityOwnerGeneration
            || !StringComparer.Ordinal.Equals(
                recovery.StableType, prepare.Object.StableType)
            || string.IsNullOrWhiteSpace(recovery.ExpectedStoreVersion))
            throw Conflict(
                "Command 40 standalone identity does not match its root.");
    }

    internal bool TryTakeStagingPermit(
        ZLinkServiceWireCodec.RelocationWireId relocationId,
        ulong targetAttemptGeneration,
        long actualPayloadBytes,
        out IDisposable permit,
        out ZLinkPreparedAggregateRelocation? preparedAggregate)
    {
        var key = new ReservationKey(relocationId, targetAttemptGeneration);
        if (!_slots.TryGetValue(key, out var slot))
        {
            permit = null!;
            preparedAggregate = null!;
            return false;
        }
        bool removeSlot;
        lock (slot.Gate)
        {
            if (slot.State != ReservationState.Staged
                || !slot.Permit.TryShrinkPayload(actualPayloadBytes))
            {
                permit = null!;
                preparedAggregate = null!;
                return false;
            }
            permit = slot.Permit;
            preparedAggregate = slot.PreparedAggregate;
            removeSlot = preparedAggregate is not null;
            slot.Permit = default;
            slot.PreparedAggregate = null;
            slot.State = ReservationState.StagingActive;
        }
        if (removeSlot)
            TryRemoveSlot(key, slot);
        return true;
    }

    internal int ExpireOffers()
    {
        var now = _timeProvider.GetUtcNow();
        var count = 0;
        foreach (var pair in _slots)
        {
            lock (pair.Value.Gate)
            {
                if (pair.Value.State != ReservationState.Offered
                    || now < pair.Value.ExpiresAt)
                    continue;
                pair.Value.State = ReservationState.Expired;
            }
            if (TryRemoveSlot(pair.Key, pair.Value))
                count++;
        }
        return count;
    }

    private async Task SweepLoopAsync()
    {
        var interval = _acceptDeadline < TimeSpan.FromSeconds(1)
            ? _acceptDeadline : TimeSpan.FromSeconds(1);
        try
        {
            using var timer = new PeriodicTimer(interval);
            while (await timer.WaitForNextTickAsync(_stop.Token)
                       .ConfigureAwait(false))
            {
                try
                {
                    await ExpireAbandonedAsync().ConfigureAwait(false);
                }
                catch when (!_stop.IsCancellationRequested)
                {
                    // Each expired slot has already released every independent
                    // resource it could. Keep sweeping later reservations even
                    // when one external store cleanup reports a failure.
                }
            }
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested)
        {
        }
    }

    internal async ValueTask<int> ExpireAbandonedAsync()
    {
        var now = _timeProvider.GetUtcNow();
        var expired = new List<(ReservationKey Key, ReservationSlot Slot,
            ZLinkPreparedAggregateRelocation? Prepared,
            ZLinkRelocationCapacityFence? Capacity,
            ZLinkRelocationPermitPool.ZLinkRelocationPermitLease Permit)>();
        foreach (var pair in _slots)
        {
            lock (pair.Value.Gate)
            {
                if (pair.Value.State is not (ReservationState.Offered
                        or ReservationState.Accepted
                        or ReservationState.Streaming
                        or ReservationState.Staged)
                    || now < pair.Value.ExpiresAt)
                    continue;
                var permit = pair.Value.Permit;
                pair.Value.Permit = default;
                expired.Add((pair.Key, pair.Value,
                    pair.Value.PreparedAggregate,
                    pair.Value.CapacityFence, permit));
                pair.Value.CapacityFence = null;
                pair.Value.PreparedAggregate = null;
                pair.Value.State = ReservationState.Expired;
            }
        }
        var count = 0;
        List<Exception>? cleanupFailures = null;
        foreach (var item in expired)
        {
            if (!TryRemoveSlot(item.Key, item.Slot))
                continue;
            count++;
            try
            {
                await CleanupRemovedSlotResourcesAsync(
                        item.Slot,
                        item.Prepared,
                        item.Capacity,
                        item.Permit)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (cleanupFailures ??= []).Add(exception);
            }
        }
        if (cleanupFailures is not null)
            throw new AggregateException(
                "One or more abandoned relocation reservations failed cleanup.",
                cleanupFailures);
        return count;
    }

    public async ValueTask DisposeAsync()
    {
        _stop.Cancel();
        await _sweepLoop.ConfigureAwait(false);
        List<Exception>? cleanupFailures = null;
        foreach (var pair in _slots.ToArray())
        {
            if (!TryRemoveSlot(pair.Key, pair.Value))
                continue;
            ZLinkPreparedAggregateRelocation? prepared;
            ZLinkRelocationCapacityFence? capacity;
            ZLinkRelocationPermitPool.ZLinkRelocationPermitLease permit;
            lock (pair.Value.Gate)
            {
                prepared = pair.Value.PreparedAggregate;
                capacity = pair.Value.CapacityFence;
                pair.Value.CapacityFence = null;
                pair.Value.PreparedAggregate = null;
                permit = pair.Value.Permit;
                pair.Value.Permit = default;
                pair.Value.State = ReservationState.Expired;
            }
            try
            {
                await CleanupRemovedSlotResourcesAsync(
                        pair.Value, prepared, capacity, permit)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (cleanupFailures ??= []).Add(exception);
            }
        }
        _stop.Dispose();
        if (cleanupFailures is not null)
            throw new AggregateException(
                "One or more relocation reservations failed disposal cleanup.",
                cleanupFailures);
    }

    private async ValueTask<ZLinkRelocationCapacityFence> ReserveCapacityAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        ReservationKey key,
        CancellationToken cancellationToken)
    {
        await ValidateCoordinatorAsync(prepare.Coordinator, cancellationToken)
            .ConfigureAwait(false);
        var authorityKey = AuthorityKey(prepare.Object);
        var read = await _store.ReadAuthorityAsync(
                authorityKey, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found)
            throw Conflict("Command 40 references missing authority.");
        var authority = found.Snapshot;
        ValidateAuthority(prepare, authority);
        var request = new ZLinkRelocationCapacityReservationRequest(
            StableReservationId(key), authorityKey, authority.StoreVersion,
            authority.Allocation.ObjectKind, authority.Allocation.StableType,
            authority.Allocation.Descriptor,
            authority.Allocation.DescriptorLifecycleGeneration,
            new ZLinkLocationOwnerToken(authority.OwnerId,
                checked((ulong)authority.OwnerLeaseGeneration)),
            new ZLinkMeshNodeDescriptorKey(_meshName, _localNodeRid),
            _localNodeGeneration,
            new ZLinkLocationOwnerToken(prepare.Candidate.OwnerId,
                prepare.Candidate.OwnerLeaseGeneration),
            authority.Allocation.Capacity);
        return await _store.ReserveRelocationCapacityAsync(
                request, cancellationToken).ConfigureAwait(false) switch
        {
            ZLinkRelocationCapacityReserveResult.Reserved value => value.Fence,
            ZLinkRelocationCapacityReserveResult.AlreadyReserved value =>
                value.Fence,
            ZLinkRelocationCapacityReserveResult.PlacementCapacityExhausted =>
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                    "The relocation target has no placement capacity.", true),
            ZLinkRelocationCapacityReserveResult.TargetUnavailable =>
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RelocationTargetUnavailable,
                    "The relocation target owner fence is unavailable.", true),
            _ => throw Conflict(
                "The relocation authority changed while reserving capacity.")
        };
    }


    private async ValueTask<ZLinkPreparedAggregateRelocation> PrepareAggregateAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        CancellationToken cancellationToken)
    {
        await ValidateCoordinatorAsync(prepare.Coordinator, cancellationToken)
            .ConfigureAwait(false);
        var root = prepare.Root ?? throw new InvalidDataException();
        var relocationStore = _relocationStore
            ?? throw Conflict("User SPOT aggregate relocation requires a Relocation Store.");
        var tree = await ZLinkRelocationTreeStore.ReadAsync(
                relocationStore, root.Reference,
                root.ChecksumCrc32c, cancellationToken)
            .ConfigureAwait(false);
        if (tree.Envelope.CanonicalLogicalStream.IsEmpty)
            throw Conflict("Command 30 requires a canonical relocation root.");
        var orderedStates = tree.Envelope.Participants
            .OrderBy(static state => state.CanonicalParticipantId)
            .ToArray();
        var orderedAllowances = prepare.Participants
            .OrderBy(static participant => participant.ParticipantId)
            .ToArray();
        if (orderedStates.Length != orderedAllowances.Length
            || prepare.RequiredMessages != orderedAllowances.Aggregate(
                0UL, static (sum, value) => checked(
                    sum + value.AllowanceMessages))
            || prepare.RequiredBytes != checked((ulong)
                ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                    tree.Envelope)))
            throw Conflict(
                "Command 40 allowance totals do not match the exact root.");
        for (var index = 0; index < orderedStates.Length; index++)
        {
            var state = orderedStates[index];
            var allowance = orderedAllowances[index];
            var acceptedBytes = state.AcceptedJobs.Aggregate(
                0UL, static (sum, job) => checked(
                    sum + (ulong)job.Payload.Length));
            if (state.CanonicalParticipantId == 0
                || allowance.ParticipantId
                   != state.CanonicalParticipantId
                || allowance.AllowanceMessages
                   != checked((ulong)state.AcceptedJobs.Count)
                || allowance.AllowanceBytes != acceptedBytes)
                throw Conflict(
                    "Command 40 participant allowance does not match the exact root.");
        }
        var participants = new List<ZLinkAggregateRelocationParticipant>(
            tree.Envelope.Participants.Count);
        foreach (var state in tree.Envelope.Participants)
        {
            var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
                state.RecoveryPayload.Span);
            if (recovery.ObjectKind != state.ObjectKind
                || recovery.ObjectGeneration != state.ObjectGeneration
                || recovery.AuthorityOwnerGeneration
                   != state.AuthorityOwnerGeneration)
                throw Conflict(
                    "Canonical participant recovery does not match its state.");
            var read = await _store.ReadAuthorityAsync(
                    recovery.AuthorityKey, cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration != recovery.ObjectGeneration
                || found.Snapshot.AuthorityOwnerGeneration
                   != recovery.AuthorityOwnerGeneration
                || !StringComparer.Ordinal.Equals(
                    found.Snapshot.StoreVersion,
                    recovery.ExpectedStoreVersion)
                || found.Snapshot.Allocation.ObjectKind != recovery.ObjectKind
                || !StringComparer.Ordinal.Equals(
                    found.Snapshot.Allocation.StableType, recovery.StableType)
                || found.Snapshot.Allocation.Descriptor.Rid
                   != prepare.SourceNodeRid
                || found.Snapshot.Allocation.DescriptorLifecycleGeneration
                   != prepare.SourceNodeGeneration
                || found.Snapshot.OwnerLeaseGeneration <= 0
                || checked((ulong)found.Snapshot.OwnerLeaseGeneration)
                   != prepare.Coordinator.LeaseGeneration)
                throw Conflict(
                    $"Canonical authority '{recovery.AuthorityKey.Value}' changed before target prepare.");
            participants.Add(new ZLinkAggregateRelocationParticipant(
                state with
                {
                    AuthorityKey = recovery.AuthorityKey,
                    ObjectKind = recovery.ObjectKind,
                    ObjectGeneration = recovery.ObjectGeneration,
                    AuthorityOwnerGeneration =
                        recovery.AuthorityOwnerGeneration
                },
                recovery.ExpectedStoreVersion,
                ZLinkAuthorityGenerationTransition.NewOwner,
                recovery.AuthorityPayload,
                recovery.MembershipMutation));
        }
        var spot = participants.Single(static participant =>
            participant.Envelope.ObjectKind is
                ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var spotRecovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            spot.Envelope.RecoveryPayload.Span);
        if (spot.Envelope.ObjectKind != ZLinkPlacementObjectKind.UserSpot
            || prepare.Object.Kind != 2
            || AuthorityKey(prepare.Object) != spotRecovery.AuthorityKey
            || prepare.Object.ObjectGeneration
               != spotRecovery.ObjectGeneration
            || prepare.Object.ExpectedAuthorityOwnerGeneration
               != spotRecovery.AuthorityOwnerGeneration
            || prepare.Object.StableType.Length != 0
               && !StringComparer.Ordinal.Equals(
                   prepare.Object.StableType, spotRecovery.StableType)
            || !StringComparer.Ordinal.Equals(
                prepare.Coordinator.ExpectedAuthorityStoreVersion,
                spotRecovery.ExpectedStoreVersion))
            throw Conflict(
                "Command 40 object fence does not match the exact User SPOT root.");
        var capacity = new ZLinkCapacityVector(
            participants.Count(static participant =>
                participant.Envelope.ObjectKind
                == ZLinkPlacementObjectKind.Actor),
            1,
            new ZLinkSpotTypeCapacityDelta(
                spot.Envelope.ObjectKind, spotRecovery.StableType, 1));
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            _store, _relocationStore!);
        var prepared = await coordinator.PrepareAsync(
                new ZLinkAggregateRelocationRequest(
                    tree.Envelope.AggregateId,
                    tree.Envelope.AggregateGeneration,
                    participants,
                    new ZLinkMeshNodeDescriptorKey(
                        _meshName, prepare.Candidate.NodeRid),
                    prepare.Candidate.NodeGeneration,
                    capacity,
                    new ZLinkLocationOwnerToken(
                        prepare.Candidate.OwnerId,
                        prepare.Candidate.OwnerLeaseGeneration),
                    tree.Envelope),
                cancellationToken)
            .ConfigureAwait(false);
        if (prepared.Relocation.Reference != root.Reference
            || prepared.Relocation.ChecksumCrc32c != root.ChecksumCrc32c)
        {
            await coordinator.AbortAsync(prepared).ConfigureAwait(false);
            throw Conflict(
                "Prepared aggregate does not preserve the accepted relocation root.");
        }
        return prepared;
    }

    private async ValueTask ValidateCoordinatorAsync(
        ZLinkServiceWireCodec.RelocationCoordinatorFence coordinator,
        CancellationToken cancellationToken)
    {
        var lease = await _store.ReadOwnerLeaseAsync(coordinator.OwnerId,
                cancellationToken).ConfigureAwait(false);
        if (lease is not ZLinkOwnerLeaseReadResult.Found found
            || found.Token.LeaseGeneration
               != checked((long)coordinator.LeaseGeneration)
            || found.LeaseExpiresAt <= found.StoreNow)
            throw Conflict("The relocation coordinator owner lease is stale.");
        string? continuation = null;
        do
        {
            var page = await _store.ListMeshNodesAsync(_meshName,
                    new ZLinkPageRequest(1000, continuation), cancellationToken)
                .ConfigureAwait(false);
            if (page.Items.Any(descriptor =>
                    descriptor.Rid == coordinator.NodeRid
                    && descriptor.LifecycleGeneration
                       == coordinator.NodeGeneration
                    && descriptor.OwnerId == coordinator.OwnerId
                    && descriptor.LeaseGeneration
                       == checked((long)coordinator.LeaseGeneration)))
                return;
            continuation = page.ContinuationToken;
        } while (continuation is not null);
        throw Conflict("The relocation coordinator node fence is stale.");
    }

    private void ValidatePrepareRoute(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid)
    {
        ArgumentNullException.ThrowIfNull(prepare);
        if (prepare.InitiatorRole != 1
            || prepare.SourceNodeRid != authenticatedSourceNodeRid
            || prepare.Candidate.NodeRid != _localNodeRid
            || prepare.Candidate.NodeGeneration != _localNodeGeneration)
            throw Conflict("Command 40 role or node fence is invalid.");
    }

    private static void ValidateAuthority(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        ZLinkAuthoritySnapshot authority)
    {
        if (!StringComparer.Ordinal.Equals(authority.StoreVersion,
                prepare.Coordinator.ExpectedAuthorityStoreVersion)
            || authority.ObjectGeneration != prepare.Object.ObjectGeneration
            || authority.AuthorityOwnerGeneration
               != prepare.Object.ExpectedAuthorityOwnerGeneration
            || authority.Allocation.ObjectKind != ObjectKind(prepare.Object.Kind)
            || prepare.Object.StableType.Length != 0
               && !StringComparer.Ordinal.Equals(authority.Allocation.StableType,
                   prepare.Object.StableType)
            || authority.Allocation.Descriptor.Rid != prepare.SourceNodeRid
            || authority.Allocation.DescriptorLifecycleGeneration
               != prepare.SourceNodeGeneration
            || prepare.Object.Kind == 1
            && (!ZLinkCanonicalRelocationAuthorityStateCodec.TryRead(
                    authority.Payload.Span,
                    out var captured)
                || captured.Phase != 2
                || captured.RelocationHigh != prepare.RelocationId.High
                || captured.RelocationLow != prepare.RelocationId.Low
                || captured.TargetAttemptGeneration != 0
                || !StringComparer.Ordinal.Equals(
                    captured.RelocationReference,
                    prepare.Root?.Reference)
                || captured.RelocationChecksumCrc32c
                   != prepare.Root?.ChecksumCrc32c))
            throw Conflict("Command 40 does not match current authority.");
    }

    private static bool MatchesAcceptance(
        ReservationSlot slot,
        ZLinkServiceWireCodec.RelocationReadyRecord value,
        RoutingId authenticatedSourceNodeRid)
    {
        var offer = slot.Offer!;
        if (value.Role != 1
            || authenticatedSourceNodeRid != slot.AuthenticatedSourceNodeRid
            || value.RelocationId != offer.RelocationId
            || value.TargetAttemptGeneration != offer.TargetAttemptGeneration
            || value.RoundKind != offer.RoundKind
            || value.Coordinator != offer.Coordinator
            || value.Candidate != offer.Candidate
            || value.Object != offer.Object
            || value.OfferedMessages != 0 || value.OfferedBytes != 0
            || value.SourceNodeGeneration != offer.SourceNodeGeneration
            || value.TargetNodeGeneration != offer.TargetNodeGeneration
            || value.ReservationGeneration != offer.ReservationGeneration
            || value.Root != offer.Root
            || value.ApplicationVersion != offer.ApplicationVersion
            || !SequenceEqual(slot.Prepare.Participants, value.Participants)
            || !SequenceEqual(offer.ParticipantProgress,
                value.ParticipantProgress))
            return false;
        ulong messages = 0;
        ulong bytes = 0;
        for (var index = 0; index < value.Participants.Count; index++)
        {
            messages = checked(messages
                + value.Participants[index].AllowanceMessages);
            bytes = checked(bytes + value.Participants[index].AllowanceBytes);
            if (value.Participants[index].ParticipantId
                != value.ParticipantProgress[index].ParticipantId)
                return false;
        }
        return messages <= offer.OfferedMessages && bytes <= offer.OfferedBytes;
    }

    private static bool SequenceEqual<T>(IReadOnlyList<T> left,
        IReadOnlyList<T> right) where T : notnull
    {
        if (left.Count != right.Count) return false;
        for (var index = 0; index < left.Count; index++)
            if (!EqualityComparer<T>.Default.Equals(left[index], right[index]))
                return false;
        return true;
    }

    private static ulong SumBytes(
        IReadOnlyList<ZLinkServiceWireCodec.RelocationParticipantRecord> values)
    {
        ulong result = 0;
        foreach (var value in values)
            result = checked(result + value.AllowanceBytes);
        return result;
    }

    private static ZLinkAuthorityKey AuthorityKey(
        ZLinkServiceWireCodec.RelocationObjectRecord value) =>
        ObjectKind(value.Kind) == ZLinkPlacementObjectKind.Actor
            ? ZLinkActorAuthorityPayloadCodec.AuthorityKey(value.ObjectId)
            : ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(value.ObjectId);

    private static ZLinkPlacementObjectKind ObjectKind(byte kind) => kind switch
    {
        1 => ZLinkPlacementObjectKind.Actor,
        2 => ZLinkPlacementObjectKind.UserSpot,
        3 => ZLinkPlacementObjectKind.InstanceSpot,
        _ => throw Conflict("Command 40 object kind is invalid.")
    };

    private static Guid StableReservationId(ReservationKey key)
    {
        Span<byte> source = stackalloc byte[24];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(source,
            key.RelocationId.High);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(source[8..],
            key.RelocationId.Low);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(source[16..],
            key.TargetAttemptGeneration);
        Span<byte> digest = stackalloc byte[32];
        SHA256.HashData(source, digest);
        return new Guid(digest[..16]);
    }

    internal static ZLinkRelocationCapacityFence CapacityFence(
        ZLinkServiceWireCodec.RelocationWireId relocationId,
        ulong targetAttemptGeneration) => new(
        StableReservationId(new ReservationKey(
            relocationId,
            targetAttemptGeneration)).ToString("N"));

    private static InvalidDataException Conflict(string message) => new(message);

    private readonly record struct ReservationKey(
        ZLinkServiceWireCodec.RelocationWireId RelocationId,
        ulong TargetAttemptGeneration);

    private sealed class ReservationSlot(
        byte[] fingerprint,
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid)
    {
        internal object Gate { get; } = new();
        internal byte[] Fingerprint { get; } = fingerprint;
        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare { get; } =
            prepare;
        internal RoutingId AuthenticatedSourceNodeRid { get; } =
            authenticatedSourceNodeRid;
        internal TaskCompletionSource<ZLinkServiceWireCodec.RelocationReadyRecord>
            OfferCompletion { get; } = new(
                TaskCreationOptions.RunContinuationsAsynchronously);
        internal ReservationState State { get; set; } = ReservationState.Reserving;
        internal ZLinkPreparedAggregateRelocation? PreparedAggregate { get; set; }
        internal ZLinkRelocationCapacityFence? CapacityFence { get; set; }
        internal ZLinkRelocationPermitPool.ZLinkRelocationPermitLease Permit { get; set; }
        internal ZLinkServiceWireCodec.RelocationReadyRecord? Offer { get; set; }
        internal ZLinkServiceWireCodec.RelocationReadyRecord? Acceptance { get; set; }
        internal ZLinkServiceWireCodec.RelocationReservedRecord? Reserved { get; set; }
        internal Task<ZLinkServiceWireCodec.RelocationReservedRecord>?
            AcceptanceOperation { get; set; }
        internal Dictionary<ulong, SortedDictionary<ulong, byte[]>> Records
            { get; } = new();
        internal Dictionary<ulong, ulong> RecordBytes { get; } = new();
        internal IReadOnlyDictionary<ulong, IReadOnlyList<byte[]>>?
            ExpectedRecords { get; set; }
        internal ZLinkRelocationEnvelope? FinalEnvelope { get; set; }
        internal ZLinkServiceWireCodec.RelocationRootRecord? FinalRoot { get; set; }
        internal ZLinkAuthoritySnapshot? FinalAuthority { get; set; }
        internal bool SealRequested { get; set; }
        internal DateTimeOffset ExpiresAt { get; set; }
        internal int CapacityReleased;
    }

    private sealed class ReservationTerminal(
        byte[] prepareFingerprint,
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid,
        ZLinkServiceWireCodec.RelocationReadyRecord offer,
        byte[] acceptanceFingerprint,
        ZLinkServiceWireCodec.RelocationReservedRecord reserved,
        byte[] sealFingerprint,
        IReadOnlyDictionary<ulong, IReadOnlyDictionary<ulong, byte[]>>
            recordDigests,
        DateTimeOffset expiresAt)
    {
        private readonly object _gate = new();
        private byte[]? _completeFingerprint;

        internal byte[] PrepareFingerprint { get; } = prepareFingerprint;
        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare { get; } =
            prepare;
        internal RoutingId AuthenticatedSourceNodeRid { get; } =
            authenticatedSourceNodeRid;
        internal ZLinkServiceWireCodec.RelocationReadyRecord Offer { get; } = offer;
        internal byte[] AcceptanceFingerprint { get; } = acceptanceFingerprint;
        internal ZLinkServiceWireCodec.RelocationReservedRecord Reserved { get; } =
            reserved;
        internal DateTimeOffset ExpiresAt { get; } = expiresAt;

        internal ZLinkServiceWireCodec.RelocationAckRecord ReplayAck(
            ZLinkServiceWireCodec.RelocationDataRecord data,
            RoutingId sourceNodeRid)
        {
            if (sourceNodeRid != AuthenticatedSourceNodeRid
                || data.SenderRole != 1
                || data.Coordinator != Prepare.Coordinator
                || !recordDigests.TryGetValue(data.ParticipantId,
                    out var participant)
                || !participant.TryGetValue(data.Sequence, out var digest)
                || !digest.AsSpan().SequenceEqual(SHA256.HashData(
                    data.FrozenRecord.Encoded.Span)))
                throw Conflict("A terminal command 31 retry changed fields.");
            var highWater = Prepare.Participants.Single(value =>
                value.ParticipantId == data.ParticipantId).AllowanceMessages;
            return new ZLinkServiceWireCodec.RelocationAckRecord(
                data.RelocationId, data.TargetAttemptGeneration,
                data.Coordinator, 2, data.ParticipantId, highWater);
        }

        internal void ValidateSeal(
            ZLinkServiceWireCodec.RelocationSealRecord seal,
            RoutingId sourceNodeRid)
        {
            if (sourceNodeRid != AuthenticatedSourceNodeRid
                || !sealFingerprint.AsSpan().SequenceEqual(SHA256.HashData(
                    ZLinkServiceWireCodec.EncodeRelocationSeal(seal))))
                throw Conflict("A terminal command 34 retry changed fields.");
        }

        internal void AcceptComplete(
            ZLinkServiceWireCodec.RelocationCompleteRecord complete,
            RoutingId sourceNodeRid)
        {
            if (sourceNodeRid != AuthenticatedSourceNodeRid
                || complete.Coordinator != Prepare.Coordinator
                || complete.Source.OwnerId != Prepare.Coordinator.OwnerId
                || complete.Source.LeaseGeneration
                   != Prepare.Coordinator.LeaseGeneration
                || complete.Source.NodeRid != Prepare.SourceNodeRid
                || complete.Source.NodeGeneration
                   != Prepare.SourceNodeGeneration)
                throw Conflict("Command 35 does not match the completed attempt.");
            var fingerprint = SHA256.HashData(
                ZLinkServiceWireCodec.EncodeRelocationComplete(complete));
            lock (_gate)
            {
                if (_completeFingerprint is { } prior
                    && !prior.AsSpan().SequenceEqual(fingerprint))
                    throw Conflict("A command 35 retry changed fields.");
                _completeFingerprint ??= fingerprint;
            }
        }
    }

    private enum ReservationState
    {
        Reserving,
        Offered,
        Accepting,
        Accepted,
        Streaming,
        Staged,
        StagingActive,
        Completed,
        Expired
    }
}
