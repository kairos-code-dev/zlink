// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

/// <summary>
///     The readiness domains a MeshNode can surface. Maps to
///     <c>zlink_mesh_ready_domain_mask_t</c>.
/// </summary>
[Flags]
public enum MeshReadyDomains : uint
{
    /// <summary>No domain.</summary>
    None = 0,

    /// <summary>Application-plane traffic.</summary>
    Application = 1u << 0,

    /// <summary>Infrastructure-plane traffic.</summary>
    Infrastructure = 1u << 1,

    /// <summary>Both application and infrastructure planes.</summary>
    All = Application | Infrastructure
}

/// <summary>
///     Which kind of owner a ready record belongs to. Maps to
///     <c>zlink_mesh_owner_kind_t</c>.
/// </summary>
public enum MeshOwnerKind
{
    /// <summary>The MeshNode itself.</summary>
    Node = 1,

    /// <summary>A spot.</summary>
    Spot = 2,

    /// <summary>An actor.</summary>
    Actor = 3
}

/// <summary>
///     The kind of a received record. Maps to <c>zlink_mesh_record_kind_t</c>.
/// </summary>
public enum MeshRecordKind
{
    /// <summary>Node-addressed send.</summary>
    NodeSend = 1,

    /// <summary>Node-addressed request.</summary>
    NodeRequest = 2,

    /// <summary>Channel-addressed send.</summary>
    ChannelSend = 3,

    /// <summary>Channel-addressed request.</summary>
    ChannelRequest = 4,

    /// <summary>Spot-addressed send.</summary>
    SpotSend = 5,

    /// <summary>Spot-addressed request.</summary>
    SpotRequest = 6,

    /// <summary>Spot logical multicast.</summary>
    SpotMulticast = 7,

    /// <summary>Spot control record.</summary>
    SpotControl = 8,

    /// <summary>Actor-addressed send.</summary>
    ActorSend = 9,

    /// <summary>Actor-addressed request.</summary>
    ActorRequest = 10,

    /// <summary>Operation completion.</summary>
    Completion = 11,

    /// <summary>Send-ready notification.</summary>
    SendReady = 12,

    /// <summary>Actor transfer control.</summary>
    TransferControl = 13
}

/// <summary>
///     The kind of operation a completion or record refers to. Maps to
///     <c>zlink_mesh_operation_kind_t</c>.
/// </summary>
public enum MeshOperationKind
{
    /// <summary>Node request.</summary>
    NodeRequest = 1,

    /// <summary>Channel request.</summary>
    ChannelRequest = 2,

    /// <summary>Spot request.</summary>
    SpotRequest = 3,

    /// <summary>Actor request.</summary>
    ActorRequest = 4,

    /// <summary>Actor lookup.</summary>
    ActorLookup = 5,

    /// <summary>Actor destroy.</summary>
    ActorDestroy = 6,

    /// <summary>Actor join.</summary>
    ActorJoin = 7,

    /// <summary>Actor leave.</summary>
    ActorLeave = 8,

    /// <summary>Stream bind.</summary>
    StreamBind = 9,

    /// <summary>Stream unbind.</summary>
    StreamUnbind = 10,

    /// <summary>Stream close.</summary>
    StreamClose = 11
}

/// <summary>
///     Identifies an in-flight MeshNode operation. Maps to
///     <c>zlink_mesh_operation_id_t</c>.
/// </summary>
/// <param name="High">The high 64 bits.</param>
/// <param name="Low">The low 64 bits.</param>
public readonly record struct MeshOperationId(ulong High, ulong Low);

/// <summary>
///     One entry in a drained ready index: an owner that has readable traffic.
///     Maps to <c>zlink_mesh_ready_record_t</c>.
/// </summary>
/// <param name="OwnerKind">Whether the owner is the node, a spot, or an actor.</param>
/// <param name="Domain">Which planes are ready.</param>
/// <param name="SpotRid">The owning spot routing id, when the owner is a spot.</param>
/// <param name="Actor">The owning actor, when the owner is an actor.</param>
public readonly record struct MeshReadyRecord(
    MeshOwnerKind OwnerKind,
    MeshReadyDomains Domain,
    RoutingId SpotRid,
    ActorRef Actor);

/// <summary>
///     One received message record inside a receive batch. Maps to
///     <c>zlink_mesh_receive_record_t</c>. Reply to request records with
///     <see cref="Reply" />.
/// </summary>
public readonly struct MeshReceiveRecord
{
    private readonly ZlinkMeshReplyToken _replyToken;

    internal MeshReceiveRecord(MeshRecordKind kind, MeshReadyDomains domain,
        RoutingId sourceNodeRid, RoutingId sourceSpotRid, ActorRef sourceActor,
        MeshOperationId operationId, MeshOperationKind operationKind,
        string? channelName, string? topic, byte[]? applicationMetadata,
        int partOffset, int partCount, int terminalResult, int failureErrno,
        ZlinkMeshReplyToken replyToken, MeshRecordPayload? kindData)
    {
        Kind = kind;
        Domain = domain;
        SourceNodeRid = sourceNodeRid;
        SourceSpotRid = sourceSpotRid;
        SourceActor = sourceActor;
        OperationId = operationId;
        OperationKind = operationKind;
        ChannelName = channelName;
        Topic = topic;
        ApplicationMetadata = applicationMetadata;
        PartOffset = partOffset;
        PartCount = partCount;
        TerminalResult = terminalResult;
        FailureErrno = failureErrno;
        _replyToken = replyToken;
        KindData = kindData;
    }

    /// <summary>Gets the record kind.</summary>
    public MeshRecordKind Kind { get; }

    /// <summary>Gets the planes this record belongs to.</summary>
    public MeshReadyDomains Domain { get; }

    /// <summary>Gets the routing id of the source node.</summary>
    public RoutingId SourceNodeRid { get; }

    /// <summary>Gets the routing id of the source spot.</summary>
    public RoutingId SourceSpotRid { get; }

    /// <summary>Gets the source actor, when present.</summary>
    public ActorRef SourceActor { get; }

    /// <summary>Gets the operation id, for completion and request records.</summary>
    public MeshOperationId OperationId { get; }

    /// <summary>Gets the operation kind.</summary>
    public MeshOperationKind OperationKind { get; }

    /// <summary>Gets the channel name, when the record is channel-addressed.</summary>
    public string? ChannelName { get; }

    /// <summary>Gets the topic, for multicast records.</summary>
    public string? Topic { get; }

    /// <summary>Gets the application metadata bytes, when present.</summary>
    public byte[]? ApplicationMetadata { get; }

    /// <summary>Gets the offset of this record's first part in the batch parts.</summary>
    public int PartOffset { get; }

    /// <summary>Gets the number of parts this record carries.</summary>
    public int PartCount { get; }

    /// <summary>Gets the terminal result, for completion records.</summary>
    public int TerminalResult { get; }

    /// <summary>Gets the failure errno, for completion records.</summary>
    public int FailureErrno { get; }

    /// <summary>
    ///     Gets the record's typed <c>kind_data</c> payload, decoded from the
    ///     Core-owned batch view at receive time, or null when the record kind
    ///     carries none. The concrete type depends on <see cref="Kind" />: an
    ///     <see cref="ActorControlRecord" /> for spot-control lifecycle records,
    ///     an <see cref="ActorJoinCompletion" /> for actor-join completions, a
    ///     <see cref="MeshSendReadyData" /> for send-ready records, or an
    ///     <see cref="ActorTransferControlRecord" /> for transfer-control records.
    /// </summary>
    public MeshRecordPayload? KindData { get; }

    /// <summary>Gets the actor lifecycle payload, when this is a spot-control record.</summary>
    public ActorControlRecord? ActorControl => KindData as ActorControlRecord;

    /// <summary>Gets the join completion payload, when this is an actor-join completion.</summary>
    public ActorJoinCompletion? JoinCompletion => KindData as ActorJoinCompletion;

    /// <summary>Gets the send-ready payload, when this is a send-ready record.</summary>
    public MeshSendReadyData? SendReady => KindData as MeshSendReadyData;

    /// <summary>Gets the transfer-control payload, when this is a transfer-control record.</summary>
    public ActorTransferControl? TransferControl =>
        (KindData as ActorTransferControlRecord)?.Control;

    /// <summary>
    ///     Replies to a request record. The reply parts are consumed on a
    ///     successful submit. Use <see cref="ReplyJoin" /> (or
    ///     <see cref="Accept" /> / <see cref="Reject" />) for actor-join
    ///     admission records — Core rejects those routes here with
    ///     <c>EINVAL</c>.
    /// </summary>
    public SubmitResult Reply(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        var token = _replyToken;
        var captured = 0;
        NativeMessageParts.SubmitClonedVector(parts, nameof(parts),
            (nativeParts, partCount) =>
            {
                var rc = NativeMethods.zlink_mesh_reply(ref token, nativeParts,
                    partCount, (int)flags);
                captured = rc;
                return rc;
            }, null);
        return (SubmitResult)captured;
    }

    /// <summary>
    ///     Admits or rejects an actor-join request record (a
    ///     <see cref="MeshRecordKind.SpotControl" /> record with operation kind
    ///     <see cref="MeshOperationKind.ActorJoin" />). Routes through Core's
    ///     dedicated <c>zlink_actor_join_reply</c> path; the reply parts are
    ///     consumed on a successful submit. Only
    ///     <see cref="ActorJoinResult.Accepted" /> commits membership.
    /// </summary>
    public SubmitResult ReplyJoin(ActorJoinResult result,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (Kind != MeshRecordKind.SpotControl
            || OperationKind != MeshOperationKind.ActorJoin)
            throw new InvalidOperationException(
                "ReplyJoin is only valid for actor-join admission records " +
                "(SpotControl / ActorJoin).");

        var token = _replyToken;
        var captured = 0;
        NativeMessageParts.SubmitClonedVector(parts, nameof(parts),
            (nativeParts, partCount) =>
            {
                var rc = NativeMethods.zlink_actor_join_reply(ref token,
                    (int)result, nativeParts, partCount, (int)flags);
                captured = rc;
                return rc;
            }, null);
        return (SubmitResult)captured;
    }

    /// <summary>Admits an actor-join request. See <see cref="ReplyJoin" />.</summary>
    public SubmitResult Accept(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        return ReplyJoin(ActorJoinResult.Accepted, parts, flags);
    }

    /// <summary>Rejects an actor-join request. See <see cref="ReplyJoin" />.</summary>
    public SubmitResult Reject(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        return ReplyJoin(ActorJoinResult.Rejected, parts, flags);
    }
}

/// <summary>
///     The admission outcome for an actor-join request. Maps to
///     <c>zlink_actor_join_result_t</c>. Only <see cref="Accepted" /> commits
///     spot membership.
/// </summary>
public enum ActorJoinResult
{
    /// <summary>Admit the actor into the spot.</summary>
    Accepted = 0,

    /// <summary>Reject the actor's join request.</summary>
    Rejected = 1
}

/// <summary>
///     Invoked by the MeshNode when readable traffic appears. Runs on a native
///     dispatch thread. Return the domains to keep armed.
/// </summary>
public delegate MeshReadyDomains MeshReadyHandler(MeshReadyDomains readyDomains);
