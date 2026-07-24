using Systems.Zlink;
using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.Runtime.Service;

internal enum MeshNodeState { Created = 1, Started, PartialReady, Ready, Draining, Stopped, Error }
internal enum MeshPeerSource { Manual = 1, Discovery, Mixed }
internal enum MeshPeerState { Configured = 1, Connecting, Admitted, Draining, Closed, Error }

internal sealed record MeshNodeStatus(
    MeshNodeState State, RoutingId RoutingId, string MeshName, string LocalEndpoint,
    ulong LifecycleGeneration, ulong DescriptorRevision, uint ChannelCount,
    uint ConfiguredPeerCount, uint AdmittedPeerCount, uint DrainingPeerCount,
    ulong PendingApplicationMessages, ulong PendingInfrastructureMessages,
    ulong PendingBytes, ulong MulticastSubmitted, ulong MulticastDroppedTargets,
    int LastError, ulong LastChangedMs);

internal sealed record MeshNodePeer(
    ulong ConnectionIntentId, MeshPeerSource Source, MeshPeerState State,
    RoutingId RoutingId, ulong LifecycleGeneration, ulong DescriptorRevision,
    string Endpoint, uint ChannelCount, int LastError, ulong LastChangedMs);

internal sealed record MeshPeerChannel(string Name, uint Weight);
internal readonly record struct MeshOperationId(ulong High, ulong Low);

[Flags]
internal enum MeshReadyDomains : uint
{
    None = 0,
    Application = 1,
    Infrastructure = 2,
    All = Application | Infrastructure
}

internal enum MeshOwnerKind { Node = 1, Spot, Actor }
internal enum MeshRecordKind
{
    NodeSend = 1, NodeRequest, ChannelSend, ChannelRequest, SpotSend,
    SpotRequest, SpotMulticast, SpotControl, ActorSend, ActorRequest,
    Completion, SendReady, TransferControl, InstanceSpotActivation
}

internal enum MeshOperationKind
{
    NodeRequest = 1, ChannelRequest, SpotRequest, ActorRequest, ActorLookup,
    ActorDestroy, ActorJoin, ActorLeave, StreamBind, StreamUnbind, StreamClose,
    InstanceSpotRequest, UserSpotCreate, UserSpotClose
}

internal enum ActorLifecycleKind { Created = 1, Joined, Left, Disconnected, Destroyed }
internal enum ActorJoinResult { Accepted = 0, Rejected = 1 }
internal enum MeshDestinationKind { Node = 1, Channel, Spot, Actor, BoundSession }

internal abstract record MeshRecordPayload;
internal sealed record ActorControlRecord(
    ActorLifecycleKind Kind, ActorRef PreviousActor, ActorRef CurrentActor,
    RoutingId PreviousSpotRid, RoutingId CurrentSpotRid,
    ulong PreviousSpotGeneration, ulong CurrentSpotGeneration,
    ulong PreviousMembershipEpoch, ulong CurrentMembershipEpoch,
    int ResultCode) : MeshRecordPayload;

internal sealed record ActorLocation(
    ActorRef Actor, RoutingId SpotRid, ulong SpotGeneration, ulong MembershipEpoch);

internal sealed record ActorJoinCompletion(
    ActorJoinResult JoinResult, ActorRef Actor, ActorLocation Location) : MeshRecordPayload;

internal sealed record MeshSendReadyData(
    MeshDestinationKind DestinationKind, RoutingId TargetNodeRid,
    RoutingId TargetSpotRid, ActorRef TargetActor,
    string? ChannelName) : MeshRecordPayload;

internal enum ActorTransferRole { Source = 1, Target = 2 }
internal enum ActorTransferPhase { Preparing = 1, Fenced, Committed, Activated, Aborted }
internal readonly record struct ActorTransferId(ulong High, ulong Low);
internal readonly record struct ActorTransferToken(Guid Value);
internal readonly record struct ActorTransferPrepare(
    ActorTransferRole Role, ActorTransferId TransferId, ActorRef Actor,
    ulong ExpectedMembershipEpoch, RoutingId PeerNodeRid, ulong FinalSequence,
    ulong ReserveMessageCount, ulong ReserveByteCount);
internal readonly record struct ActorTransferPrepareResult(
    ActorTransferRole Role, ActorTransferId TransferId, ActorRef Actor,
    ulong FinalSequence, ulong ReserveMessageCount, ulong ReserveByteCount);
internal readonly record struct ActorTransferControl(
    ActorTransferPhase Phase, ActorTransferRole Role, ActorTransferId TransferId,
    ActorRef Actor, ulong MembershipEpoch, ulong FinalSequence,
    int ResultCode, int FailureErrno);
internal sealed record ActorTransferControlRecord(ActorTransferControl Control) : MeshRecordPayload;

internal readonly record struct UserSpotReservationFence(
    string ReservationId,
    string ExpectedStoreVersion,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    RoutingId TargetNodeRid,
    ulong TargetNodeGeneration,
    string TargetOwnerId,
    ulong TargetOwnerLeaseGeneration,
    uint PendingCapacityDelta);

internal readonly record struct UserSpotCloseFence(
    RoutingId SpotRid,
    ulong ObjectGeneration,
    RoutingId TargetNodeRid,
    ulong TargetNodeGeneration,
    ulong AuthorityOwnerGeneration,
    string ExpectedStoreVersion);

internal readonly record struct UserSpotCreateOperation(
    ulong Correlation,
    MeshOperationId OperationId,
    RoutingId SourceNodeRid,
    ulong SourceNodeGeneration,
    RoutingId SpotRid,
    string StableType,
    UserSpotReservationFence Reservation,
    ulong DeadlineUnixMs);

internal readonly record struct UserSpotCloseOperation(
    ulong Correlation,
    MeshOperationId OperationId,
    RoutingId SourceNodeRid,
    ulong SourceNodeGeneration,
    UserSpotCloseFence Target,
    ulong DeadlineUnixMs);

internal enum UserSpotCreateResult : byte
{
    Existing = 1,
    Created = 2,
    Rejected = 3
}

internal sealed record UserSpotCreateCompletion(
    UserSpotCreateResult Result,
    RoutingId SpotRid,
    ulong ObjectGeneration) : MeshRecordPayload;

internal sealed record UserSpotCloseCompletion(bool Closed) : MeshRecordPayload;

internal readonly record struct InstanceSpotActivationTarget(
    string MeshName,
    RoutingId TargetNodeRid,
    ulong TargetNodeGeneration,
    RoutingId TargetSpotRid,
    string StableType,
    string DescriptorVersion,
    string? PlacementProfile,
    string? AffinityKey);

internal readonly record struct InstanceSpotActivationOperation(
    InstanceSpotActivationTarget Target,
    RoutingId SourceNodeRid,
    ulong SourceNodeGeneration,
    RoutingId SourceSpotRid,
    MeshOperationId OperationId,
    bool IsRequest,
    ulong ReplyRouteId,
    ulong DeadlineUnixMs);

internal sealed record InstanceSpotActivationTerminal(
    RequestResult Result,
    ServiceWireConstants.FrameworkErrorCode FailureCode,
    IReadOnlyList<ReadOnlyMemory<byte>> ReplyParts);

internal interface IInstanceSpotActivationTarget
{
    ValueTask<InstanceSpotActivationTerminal> ActivateAsync(
        InstanceSpotActivationOperation operation,
        ReadOnlyMemory<byte>? metadata,
        IReadOnlyList<ReadOnlyMemory<byte>> payload,
        CancellationToken cancellationToken);
}

internal sealed record UserSpotOperationTerminal(
    RequestResult Result,
    ServiceWireConstants.FrameworkErrorCode FailureCode,
    MeshRecordPayload? Completion = null,
    IReadOnlyList<ReadOnlyMemory<byte>>? ReplyParts = null);

internal interface IUserSpotOperationTarget
{
    ValueTask<UserSpotOperationTerminal> CreateAsync(
        UserSpotCreateOperation operation,
        CancellationToken cancellationToken);

    ValueTask<UserSpotOperationTerminal> CloseAsync(
        UserSpotCloseOperation operation,
        CancellationToken cancellationToken);
}

[Flags]
internal enum MeshMonitorEventMask : ulong
{
    None = 0,
    StateChanged = 1UL << 0,
    PeerConnecting = 1UL << 1,
    PeerAdmitted = 1UL << 2,
    PeerDraining = 1UL << 3,
    PeerClosed = 1UL << 4,
    PeerRejected = 1UL << 5,
    ChannelChanged = 1UL << 6,
    MessageSubmitted = 1UL << 7,
    MulticastCommitted = 1UL << 8,
    MulticastDropped = 1UL << 9,
    Backpressured = 1UL << 10,
    OperationCompleted = 1UL << 11,
    ProtocolError = 1UL << 12,
    ClaimRevoked = 1UL << 13,
    All = (1UL << 14) - 1
}

internal enum MeshMonitorEventKind
{
    StateChanged = 1, PeerConnecting, PeerAdmitted, PeerDraining, PeerClosed,
    PeerRejected, ChannelChanged, MessageSubmitted, MulticastCommitted,
    MulticastDropped, Backpressured, OperationCompleted, ProtocolError, ClaimRevoked
}

internal sealed record MeshMonitorEvent(
    MeshMonitorEventKind Kind, ulong TimestampMs, ulong MeshLifecycleGeneration,
    ulong MeshDescriptorRevision, MeshNodeState MeshState, RoutingId PeerRid,
    ulong PeerLifecycleGeneration, ulong PeerDescriptorRevision,
    MeshOwnerKind OwnerKind, RoutingId SpotRid, ActorRef Actor,
    string ChannelName, MeshOperationId OperationId,
    uint SnapshotRemoteTargetCount, uint AdmittedRemoteTargetCount,
    uint DroppedRemoteTargetCount, uint UnreachableRemoteTargetCount,
    uint SnapshotLocalSpotCount, uint AdmittedLocalSpotCount,
    uint DroppedLocalSpotCount, int ResultCode, int FailureErrno);

internal sealed record MeshMonitorStatus(
    MeshNodeState State, ulong PeerAdmitted, ulong PeerRejected,
    ulong SubmittedMessages, ulong CompletedOperations, ulong ProtocolErrors,
    ulong BackpressuredSubmits, ulong DroppedTargets, ulong LastSequence);

internal interface IMeshNodeMonitor : IDisposable, IAsyncDisposable
{
    MeshMonitorStatus Status();
    MeshMonitorEvent? Recv(RecvFlags flags = RecvFlags.None);
}

internal readonly record struct MeshReadyRecord(
    MeshOwnerKind OwnerKind, MeshReadyDomains Domain,
    RoutingId SpotRid, ActorRef Actor);

internal sealed class MeshReadyBatch : IDisposable
{
    private readonly List<(MeshReadyRecord Record, MeshClaim Claim)> _entries = new();
    public int Count => _entries.Count;
    public MeshReadyRecord this[int index] => _entries[index].Record;
    public MeshClaim TakeClaim(int index) => _entries[index].Claim;
    internal void Add(MeshReadyRecord record, MeshClaim claim) =>
        _entries.Add((record, claim));
    internal void Reset()
    {
        foreach (var (_, claim) in _entries)
            claim.Dispose();
        _entries.Clear();
    }
    public void Dispose() => Reset();
}

internal sealed class MeshReceiveBatch : IDisposable
{
    private readonly List<(MeshReceiveRecord Record, IReadOnlyList<Message> Parts)> _entries = new();
    public int Count => _entries.Count;
    public MeshReceiveRecord this[int index] => _entries[index].Record;
    public IReadOnlyList<Message> RetainMessage(int index) =>
        _entries[index].Parts.Select(Message.From).ToArray();
    internal void Add(MeshReceiveRecord record, IReadOnlyList<Message> parts) =>
        _entries.Add((record, parts));
    public void Reset()
    {
        foreach (var (_, parts) in _entries)
            foreach (var part in parts)
                part.Dispose();
        _entries.Clear();
    }
    public void Dispose() => Reset();
}

internal sealed class MeshClaim : IDisposable
{
    internal Func<MeshReceiveBatch, RecvFlags, bool>? Receiver { get; init; }
    internal Action? Releaser { get; init; }
    private int _disposed;
    public bool Receive(MeshReceiveBatch batch, RecvFlags flags = RecvFlags.None) =>
        Volatile.Read(ref _disposed) == 0 && (Receiver?.Invoke(batch, flags) ?? false);
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
            Releaser?.Invoke();
    }
}

internal readonly struct MeshReceiveRecord
{
    private readonly Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? _reply;
    private readonly Func<ActorJoinResult, IReadOnlyList<Message>, SendFlags, SubmitResult>?
        _joinReply;
    internal MeshReceiveRecord(
        MeshRecordKind kind, MeshReadyDomains domain, RoutingId sourceNodeRid,
        RoutingId sourceSpotRid, ulong sourceBindingGeneration, ActorRef sourceActor,
        MeshOperationId operationId, MeshOperationKind operationKind,
        string? channelName, string? topic, byte[]? applicationMetadata,
        int partOffset, int partCount, int terminalResult, int failureErrno,
        MeshRecordPayload? kindData,
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null,
        Func<ActorJoinResult, IReadOnlyList<Message>, SendFlags, SubmitResult>?
            joinReply = null)
    {
        Kind = kind; Domain = domain; SourceNodeRid = sourceNodeRid;
        SourceSpotRid = sourceSpotRid; SourceBindingGeneration = sourceBindingGeneration;
        SourceActor = sourceActor; OperationId = operationId; OperationKind = operationKind;
        ChannelName = channelName; Topic = topic; ApplicationMetadata = applicationMetadata;
        PartOffset = partOffset; PartCount = partCount; TerminalResult = terminalResult;
        FailureErrno = failureErrno; KindData = kindData; _reply = reply;
        _joinReply = joinReply;
    }
    public MeshRecordKind Kind { get; }
    public MeshReadyDomains Domain { get; }
    public RoutingId SourceNodeRid { get; }
    public RoutingId SourceSpotRid { get; }
    public ulong SourceBindingGeneration { get; }
    public ActorRef SourceActor { get; }
    public MeshOperationId OperationId { get; }
    public MeshOperationKind OperationKind { get; }
    public string? ChannelName { get; }
    public string? Topic { get; }
    public byte[]? ApplicationMetadata { get; }
    public int PartOffset { get; }
    public int PartCount { get; }
    public int TerminalResult { get; }
    public int FailureErrno { get; }
    public MeshRecordPayload? KindData { get; }
    public ActorControlRecord? ActorControl => KindData as ActorControlRecord;
    public ActorJoinCompletion? JoinCompletion => KindData as ActorJoinCompletion;
    public UserSpotCreateCompletion? UserSpotCreateCompletion =>
        KindData as UserSpotCreateCompletion;
    public UserSpotCloseCompletion? UserSpotCloseCompletion =>
        KindData as UserSpotCloseCompletion;
    public MeshSendReadyData? SendReady => KindData as MeshSendReadyData;
    public ActorTransferControl? TransferControl => (KindData as ActorTransferControlRecord)?.Control;
    public SubmitResult Reply(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None) =>
        _reply?.Invoke(parts, flags) ?? SubmitResult.Terminated;
    public SubmitResult ReplyJoin(
        ActorJoinResult result,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None) =>
        _joinReply?.Invoke(result, parts, flags) ?? Reply(parts, flags);

    internal static MeshReceiveRecord CompletionFailure(
        MeshOperationId operationId,
        RequestResult result) =>
        new(
            MeshRecordKind.Completion,
            MeshReadyDomains.Infrastructure,
            default,
            default,
            0,
            default,
            operationId,
            MeshOperationKind.NodeRequest,
            null,
            null,
            null,
            0,
            0,
            (int) result,
            0,
            null);
}

internal readonly record struct MeshPublishDetail(
    ulong SnapshotRemoteTargets, ulong AdmittedRemoteTargets,
    ulong DroppedRemoteTargets, ulong UnreachableRemoteTargets,
    ulong SnapshotLocalSpots, ulong AdmittedLocalSpots,
    ulong DroppedLocalSpots);
internal readonly record struct MeshPublishResult(SubmitResult Result, MeshPublishDetail Detail);

internal interface IMeshNode : IDisposable, IAsyncDisposable
{
    RoutingId RoutingId { get; }
    long MaxMessageSize { get; set; }
    int RouterHighWaterMark { get; set; }
    ulong MailboxMessageBudget { get; set; }
    ulong MailboxByteBudget { get; set; }
    TimeSpan? SendTimeout { get; set; }
    void SetRoutingId(RoutingId routingId);
    void SetBind(string endpoint);
    void Start();
    ulong ConnectPeer(string endpoint, RoutingId? expectedRid = null);
    void RemovePeerConnection(ulong connectionIntentId);
    void DisconnectPeer(RoutingId peerRid, ulong lifecycleGeneration = 0);
    void AddChannel(string channelName);
    void SetChannelWeight(string channelName, uint weight);
    MeshNodeStatus Status();
    MeshNodePeer[] Peers();
    MeshPeerChannel[] PeerChannels(RoutingId peerRid, ulong lifecycleGeneration);
    IMeshNodeMonitor OpenMonitor(MeshMonitorEventMask events = MeshMonitorEventMask.All);
    void SetReadyHandler(Func<MeshReadyDomains, MeshReadyDomains> handler);
    bool DrainReady(MeshReadyDomains domains, MeshReadyBatch batch, RecvFlags flags = RecvFlags.None);
    ISpot CreateSpot();
    ISpot EntrySpot();
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);
    ActorRef CreateActor(string actorId, IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default);
    bool ActorLookup(string actorId, out ActorLocation location);
    MeshOperationId DestroyActor(ActorRef actor, TimeSpan timeout = default);
    MeshOperationId JoinSpot(ActorRef actor, RoutingId targetNodeRid, RoutingId targetSpotRid,
        ulong targetSpotGeneration, IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default);
    MeshOperationId JoinEntrySpot(ActorRef actor, RoutingId targetNodeRid,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default);
    SubmitResult SendToNode(RoutingId targetRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
    SubmitResult RequestToNode(RoutingId targetRid, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
    SubmitResult SendToActor(ActorRef actor, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    SubmitResult RequestToActor(ActorRef actor, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default);
    SubmitResult SendBoundSession(ActorRef actor, IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
    MeshOperationId CloseBoundSession(ActorRef actor, ulong expectedBindingGeneration, TimeSpan timeout = default);
    ActorTransferToken PrepareActorTransfer(ActorTransferPrepare prepare,
        out ActorTransferPrepareResult result, TimeSpan timeout = default);
    void CommitActorTransfer(ActorTransferToken token, ulong newMembershipEpoch);
    void ActivateActorTransfer(ActorTransferToken token);
    void AbortActorTransfer(ActorTransferToken token);
    void SetUserSpotOperationTarget(IUserSpotOperationTarget target);
    void SetInstanceSpotActivationTarget(IInstanceSpotActivationTarget target);
    SubmitResult ActivateInstanceSpot(
        InstanceSpotActivationTarget target,
        RoutingId sourceSpotRid,
        IReadOnlyList<Message> parts,
        bool request,
        out MeshOperationId operationId,
        ulong deadlineUnixMs,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);
    SubmitResult CreateUserSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        string stableType,
        UserSpotReservationFence reservation,
        ulong deadlineUnixMs,
        out MeshOperationId operationId,
        TimeSpan timeout = default);
    SubmitResult CloseUserSpot(
        RoutingId targetNodeRid,
        UserSpotCloseFence target,
        ulong deadlineUnixMs,
        out MeshOperationId operationId,
        TimeSpan timeout = default);
    IStreamSessionService CreateStreamSessionService(IStreamSocket stream);
}

internal interface ISpot : IDisposable, IAsyncDisposable
{
    RoutingId RoutingId { get; }
    ulong LifecycleGeneration { get; }
    void SetRoutingId(RoutingId routingId);
    SpotStatus Status();
    void SetSubscription(string channelName, string topic);
    SubmitResult SendToChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
    SubmitResult RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
    MeshPublishResult Publish(string channelName, string topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);
    SubmitResult SendToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
        ulong targetSpotGeneration, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
    SubmitResult RequestToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
        ulong targetSpotGeneration, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, ReadOnlyMemory<byte> metadata = default);
}

internal readonly record struct SpotStatus(ulong LifecycleGeneration);

internal sealed record StreamSessionBinding(
    RoutingId SessionRid, ActorRef Actor, ulong BindingGeneration,
    ulong MembershipEpoch);

internal interface IStreamSessionService : IDisposable, IAsyncDisposable
{
    void Start();
    SubmitResult BindActor(RoutingId sessionRid, ActorRef actor,
        out MeshOperationId operationId, TimeSpan timeout = default);
    SubmitResult UnbindActor(RoutingId sessionRid, ActorRef actor,
        ulong expectedBindingGeneration, out MeshOperationId operationId,
        TimeSpan timeout = default);
    StreamSessionBinding[] Bindings(RoutingId sessionRid);
    SubmitResult SendToActor(RoutingId sessionRid, ActorRef actor,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None);
}
