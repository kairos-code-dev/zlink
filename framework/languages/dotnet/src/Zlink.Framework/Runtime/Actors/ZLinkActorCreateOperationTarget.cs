using System.Globalization;
using System.Security.Cryptography;
using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorCreateOperationTarget(
    IZLinkAuthorityStore authorityStore,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpotNode node,
    string meshName,
    ZLinkCodecRegistryBuilder codecs) : IActorCreateOperationTarget
{
    private static readonly TimeSpan TerminalRetention = TimeSpan.FromMinutes(5);

    public async ValueTask<ActorCreateOperationTerminal> CreateAsync(
        ActorCreateOperation operation,
        CancellationToken cancellationToken)
    {
        var operationId = new ZLinkCreationOperationId(
            operation.SourceNodeRid,
            operation.SourceNodeGeneration,
            operation.OperationId.High,
            operation.OperationId.Low);
        var replay = await authorityStore.ReadCreationTerminalAsync(
                operationId,
                cancellationToken)
            .ConfigureAwait(false);
        if (replay is ZLinkCreationTerminalReadResult.Found retained)
            return DecodeTerminal(retained.Record);

        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(operation.ActorId);
        var read = await authorityStore.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found)
            throw Stale(operation.ActorId, "The reserved Actor authority is missing.");
        var snapshot = found.Snapshot;
        ValidateFence(operation, snapshot);
        if (!ZLinkActorAuthorityPayloadCodec.TryDecode(snapshot.Payload.Span, out var authority)
            || authority.State != ZLinkActorAuthorityState.Creating
            || !string.Equals(authority.ActorId, operation.ActorId, StringComparison.Ordinal)
            || !string.Equals(authority.StableType, operation.StableType, StringComparison.Ordinal)
            || authority.NodeRid != node.RoutingId
            || authority.NodeGeneration != node.MeshStatus().LifecycleGeneration)
            throw Protocol(operation.ActorId, "The pending Actor authority payload is invalid.");
        if (snapshot.PendingCreation is not { } pending
            || !string.Equals(
                pending.ReservationId,
                operation.Reservation.ReservationId,
                StringComparison.Ordinal)
            || !ZLinkInlineCreationIntentCodec.TryDecode(
                pending.RequestContentReference,
                out var content)
            || content.Length != pending.RequestEncodedSize
            || !CryptographicOperations.FixedTimeEquals(
                SHA256.HashData(content),
                pending.RequestSha256.Span)
            || !ZLinkApplicationPayloadEnvelopeCodec.TryDecode(content, out var application))
            throw Protocol(operation.ActorId, "The immutable Actor creation content is invalid.");

        using var requestPayload = Message.From(application.Payload.Span);
        var request = ZLinkMessage.FromEnvelopePayload(
            application.ContentType,
            requestPayload,
            codecs);
        CreateActorResult prepared;
        try
        {
            prepared = await runtime.PrepareReservedActorAsync(
                    operation.ActorId,
                    operation.StableType,
                    request,
                    snapshot.ObjectGeneration,
                    snapshot.AuthorityOwnerGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            try
            {
                await runtime.DiscardReservedActorAsync(
                        operation.ActorId,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch
            {
                // The durable Failed terminal still closes the reservation.
                // Teardown reconciliation remains owned by the Actor runtime.
            }
            return await FailAsync(
                    operation,
                    operationId,
                    Reservation(operation, key, snapshot),
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (prepared.Response is { Accepted: false } rejected)
        {
            await runtime.DiscardReservedActorAsync(
                    operation.ActorId,
                    CancellationToken.None)
                .ConfigureAwait(false);
            return await CompleteAsync(
                    operation,
                    operationId,
                    Reservation(operation, key, snapshot),
                    new ActorCreateOperationTerminal(
                        RequestResult.Ok,
                        ServiceWireConstants.FrameworkErrorCode.None,
                        new ActorCreateCompletion(ActorCreateResult.Rejected, default),
                        EncodeReply(operation.Correlation, rejected.Reply)),
                    readyPayload: null,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var published = new ActorRef(
            node.RoutingId,
            operation.ActorId,
            snapshot.ObjectGeneration);
        var readyPayload = ZLinkActorAuthorityPayloadCodec.Encode(
            authority with
            {
                State = ZLinkActorAuthorityState.Ready,
                OwnerId = snapshot.OwnerId,
                OwnerLeaseGeneration = checked((ulong)snapshot.OwnerLeaseGeneration),
                MeshName = meshName,
                NodeRid = node.RoutingId,
                NodeGeneration = node.MeshStatus().LifecycleGeneration
            });
        var terminal = new ActorCreateOperationTerminal(
            RequestResult.Ok,
            ServiceWireConstants.FrameworkErrorCode.None,
            new ActorCreateCompletion(ActorCreateResult.Created, published),
            EncodeReply(operation.Correlation, prepared.Response?.Reply));
        var completed = await CompleteAsync(
                operation,
                operationId,
                Reservation(operation, key, snapshot),
                terminal,
                readyPayload,
                cancellationToken)
            .ConfigureAwait(false);
        if (completed.Completion?.Result == ActorCreateResult.Created)
            runtime.PublishReservedActor(operation.ActorId);
        else
            await runtime.DiscardReservedActorAsync(
                    operation.ActorId,
                    CancellationToken.None)
                .ConfigureAwait(false);
        return completed;
    }

    private async ValueTask<ActorCreateOperationTerminal> FailAsync(
        ActorCreateOperation operation,
        ZLinkCreationOperationId operationId,
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken)
    {
        var terminal = new ActorCreateOperationTerminal(
            RequestResult.InternalError,
            ServiceWireConstants.FrameworkErrorCode.ActorCreateFailed);
        return await CompleteAsync(
                operation,
                operationId,
                reservation,
                terminal,
                readyPayload: null,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ActorCreateOperationTerminal> CompleteAsync(
        ActorCreateOperation operation,
        ZLinkCreationOperationId operationId,
        ZLinkObjectReservation reservation,
        ActorCreateOperationTerminal terminal,
        ReadOnlyMemory<byte>? readyPayload,
        CancellationToken cancellationToken)
    {
        var envelope = ZLinkActorCreationTerminalCodec.Encode(terminal, codecs);
        var publication = new ZLinkCreationTerminalPublication(
            operationId,
            envelope,
            SHA256.HashData(envelope),
            DateTimeOffset.FromUnixTimeMilliseconds(
                    checked((long)operation.DeadlineUnixMs))
                .Add(TerminalRetention));
        ZLinkObjectCreationCompletion completion = readyPayload is { } ready
            ? new ZLinkObjectCreationCompletion.Created(ready, publication)
            : terminal.Completion?.Result == ActorCreateResult.Rejected
                ? new ZLinkObjectCreationCompletion.Rejected(publication)
                : new ZLinkObjectCreationCompletion.Failed(publication);
        var result = await authorityStore.CompleteCreationAsync(
                reservation,
                completion,
                cancellationToken)
            .ConfigureAwait(false);
        return result switch
        {
            ZLinkObjectCreationCompleteResult.Created created =>
                DecodeTerminal(created.Terminal),
            ZLinkObjectCreationCompleteResult.Rejected rejected =>
                DecodeTerminal(rejected.Terminal),
            ZLinkObjectCreationCompleteResult.Failed failed =>
                DecodeTerminal(failed.Terminal),
            ZLinkObjectCreationCompleteResult.AlreadyCompleted existing =>
                DecodeTerminal(existing.Terminal),
            _ => await ReplayAfterConflictAsync(operationId, operation.ActorId)
                .ConfigureAwait(false)
        };
    }

    private async ValueTask<ActorCreateOperationTerminal> ReplayAfterConflictAsync(
        ZLinkCreationOperationId operationId,
        string actorId)
    {
        var replay = await authorityStore.ReadCreationTerminalAsync(
                operationId,
                CancellationToken.None)
            .ConfigureAwait(false);
        if (replay is ZLinkCreationTerminalReadResult.Found found)
            return DecodeTerminal(found.Record);
        throw Stale(actorId, "Actor creation lost its reservation without a retained terminal.");
    }

    private ActorCreateOperationTerminal DecodeTerminal(ZLinkCreationTerminalRecord record)
    {
        if (record.ObjectKind != ZLinkPlacementObjectKind.Actor
            || !CryptographicOperations.FixedTimeEquals(
                SHA256.HashData(record.TerminalEnvelope.Span),
                record.TerminalEnvelopeSha256.Span)
            || !ZLinkActorCreationTerminalCodec.TryDecode(
                record.TerminalEnvelope,
                codecs,
                out var terminal))
            throw Protocol(string.Empty, "The retained Actor creation terminal is invalid.");
        return terminal;
    }

    private IReadOnlyList<ReadOnlyMemory<byte>>? EncodeReply(
        ulong correlation,
        ZLinkMessage? reply)
    {
        if (reply is null) return null;
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                string.Empty,
                string.Empty,
                ZLinkEnvelopeCodec.DefaultContentType,
                correlation.ToString(CultureInfo.InvariantCulture),
                null, null, null, null),
            reply,
            typeof(ZLinkMessage),
            codecs);
        try
        {
            return parts.Select(static part =>
                (ReadOnlyMemory<byte>)part.AsReadOnlyMemory().ToArray()).ToArray();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    private static ZLinkObjectReservation Reservation(
        ActorCreateOperation operation,
        ZLinkAuthorityKey key,
        ZLinkAuthoritySnapshot snapshot) =>
        new(
            key,
            operation.Reservation.ExpectedStoreVersion,
            operation.Reservation.ObjectGeneration,
            operation.Reservation.AuthorityOwnerGeneration,
            operation.Reservation.ReservationId,
            snapshot.Allocation.Descriptor,
            operation.Reservation.TargetNodeGeneration,
            new ZLinkLocationOwnerToken(
                operation.Reservation.TargetOwnerId,
                operation.Reservation.TargetOwnerLeaseGeneration));

    private static void ValidateFence(
        ActorCreateOperation operation,
        ZLinkAuthoritySnapshot snapshot)
    {
        var fence = operation.Reservation;
        if (snapshot.ObjectGeneration != fence.ObjectGeneration
            || snapshot.AuthorityOwnerGeneration != fence.AuthorityOwnerGeneration
            || !string.Equals(snapshot.StoreVersion, fence.ExpectedStoreVersion, StringComparison.Ordinal)
            || snapshot.Allocation.ObjectKind != ZLinkPlacementObjectKind.Actor
            || !string.Equals(snapshot.Allocation.StableType, operation.StableType, StringComparison.Ordinal)
            || snapshot.Allocation.Descriptor.Rid != fence.TargetNodeRid
            || snapshot.Allocation.DescriptorLifecycleGeneration != fence.TargetNodeGeneration
            || !string.Equals(snapshot.OwnerId, fence.TargetOwnerId, StringComparison.Ordinal)
            || snapshot.OwnerLeaseGeneration != checked((long)fence.TargetOwnerLeaseGeneration)
            || snapshot.Allocation.Capacity
            != new ZLinkCapacityVector(
                checked((int)fence.PendingCapacityDelta),
                0,
                null))
            throw Stale(operation.ActorId, "The Actor creation reservation fence is stale.");
    }

    private static ZLinkFrameworkException Stale(string actorId, string message) =>
        new(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            string.IsNullOrEmpty(actorId) ? message : $"Actor '{actorId}': {message}",
            true);

    private static ZLinkFrameworkException Protocol(string actorId, string message) =>
        new(
            ZLinkFrameworkErrorKind.RequestProtocolError,
            string.IsNullOrEmpty(actorId) ? message : $"Actor '{actorId}': {message}");
}
