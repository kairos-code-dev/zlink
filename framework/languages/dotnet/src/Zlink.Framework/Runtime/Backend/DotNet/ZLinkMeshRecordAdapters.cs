using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Backend.DotNet.Mappings;

namespace Zlink.Framework.Runtime.Backend.DotNet;

// Adapts 10.0.0 MeshReceiveRecord payloads into the framework-owned seam objects
// the actor/lifecycle/join plane already consumes.
internal static class ZLinkMeshRecordAdapters
{
    public static ZLinkBackendActorJoinRequest ToActorJoinRequest(
        MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        IReadOnlyList<Message> parts = batch.RetainMessage(index);
        var message = parts.Count > 0 ? parts[0] : Message.From(ReadOnlySpan<byte>.Empty);
        var epoch = record.ActorControl?.CurrentMembershipEpoch ?? 0;
        return new ZLinkMeshActorJoinRequest(
            record.SourceActor.ToBackend(),
            record.SourceActor.ToBackend(),
            record.SourceNodeRid,
            record.SourceSpotRid,
            epoch,
            message,
            parts,
            record);
    }

    public static ZLinkBackendSpotActorLifecycleEvent? ToLifecycleEvent(
        ActorControlRecord control)
    {
        var kind = control.Kind switch
        {
            ActorLifecycleKind.Joined => ZLinkBackendActorLifecycleEventKind.Joined,
            ActorLifecycleKind.Left => ZLinkBackendActorLifecycleEventKind.Left,
            ActorLifecycleKind.Disconnected => ZLinkBackendActorLifecycleEventKind.Disconnected,
            _ => (ZLinkBackendActorLifecycleEventKind?)null
        };
        if (kind is not { } lifecycleKind) return null;

        var info = new ZLinkBackendSpotActorLifecycleInfo(
            control.PreviousActor.ActorId is { Length: > 0 }
                ? control.PreviousActor.ToBackend()
                : null,
            control.CurrentActor.ActorId is { Length: > 0 }
                ? control.CurrentActor.ToBackend()
                : null,
            control.PreviousSpotRid,
            control.CurrentSpotRid,
            control.CurrentMembershipEpoch,
            0);
        return new ZLinkBackendSpotActorLifecycleEvent(lifecycleKind, info);
    }

    public static IReadOnlyList<ZLinkBackendActorPart> ToActorParts(
        MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        IReadOnlyList<Message> messages = batch.RetainMessage(index);
        if (messages.Count == 0) return Array.Empty<ZLinkBackendActorPart>();

        var actor = record.SourceActor.ToBackend();
        var requestId = record.OperationId.Low;
        var flags = record.Kind == MeshRecordKind.ActorRequest ? 1u : 0u;
        var parts = new ZLinkBackendActorPart[messages.Count];
        for (var i = 0; i < parts.Length; i++)
            parts[i] = new ZLinkBackendActorPart(
                actor,
                record.SourceNodeRid,
                record.SourceSpotRid,
                requestId,
                flags,
                messages[i],
                i + 1 < parts.Length);
        return parts;
    }
}

// Framework actor-join request that carries the originating pull-dispatch record
// so ReplyActorJoin can route the admission reply through MeshReceiveRecord.
internal sealed class ZLinkMeshActorJoinRequest(
    ZLinkBackendActorRef sourceActor,
    ZLinkBackendActorRef targetActor,
    RoutingId sourceNodeRid,
    RoutingId targetSpotRid,
    ulong joinEpoch,
    Message message,
    IReadOnlyList<Message> parts,
    MeshReceiveRecord record)
    : ZLinkBackendActorJoinRequest(
        sourceActor,
        targetActor,
        sourceNodeRid,
        targetSpotRid,
        joinEpoch,
        message,
        parts)
{
    private readonly MeshReceiveRecord _record = record;

    public SubmitResult ReplyJoin(int joinResultCode, IReadOnlyList<Message> parts)
    {
        var result = joinResultCode == 0
            ? ActorJoinResult.Accepted
            : ActorJoinResult.Rejected;
        return _record.ReplyJoin(result, parts);
    }
}
