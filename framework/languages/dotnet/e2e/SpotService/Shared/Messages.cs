namespace SpotService.Shared;

public static class SpotServiceNames
{
    public const string SpotChannel = "spot.service";
    public const string SpotEventTopic = "spot.service.events";
    public const string ControlChannel = "spot.control";
    public const string ExternalSpotChannel = "spot.external.play-a";
    public const string ExternalSpotChannelB = "spot.external.play-b";
    public const string ExternalClientChannel = "spot.external.client";
    public const string ExternalClientServerChannel = "spot.external.cs.client";
    public const string StreamNode = "session-stream";
    public const string TlsStreamNode = "session-stream-tls";
    public const string PlaySpotNode = "play-node";
    public const string SessionSpotNode = "session-node";
    public const string EdgeSpotNode = "edge-node";
    public const string EdgePublisherNode = "edge-publisher-node";
    public const string MultiSpotNodeA = "multi-node-a";
    public const string MultiSpotNodeB = "multi-node-b";
    public const string MultiRouteChannelA = "multi-route-a";
    public const string MultiRouteChannelB = "multi-route-b";
    public const string ActorType = "scenario-player";
    public const string ActorIdMetadata = "actor-id";
}

public sealed record JoinReq(string Key, string ActorId, string DisplayName, int Level, string[] Tags);

public sealed record JoinReply(string SpotRid, string NodeRid, string ActorId);

public sealed record EnsureActorReq(string ActorId, string DisplayName, string NodeRid);

public sealed record EnsureActorReply(string ActorId, string NodeRid, ulong Generation);

public sealed record StateReq(string Operation, int Delta);

public sealed record StateReply(string SpotRid, string NodeRid, int Value);

public sealed record StateCommand(string Marker);

public sealed record SpotStateRouteReq(string SpotRid, string Operation, int Delta);

public sealed record SpotStateCommandReq(string SpotRid, string Marker);

public sealed record SpotStateCommandReply(string SpotRid, string Marker, bool Accepted, string[] Evidence);

public sealed record EvidenceWaitRequest(
    string[] ContainsAll,
    int TimeoutMilliseconds = 10000);

public sealed record SpotMissingHandlerReq(string SpotRid);

public sealed record SpotMissingHandlerReply(string SpotRid, bool Failed, string[] Evidence);

public sealed record SpotMissingCommandReq(string SpotRid, string Marker);

public sealed record SpotMissingCommandReply(string SpotRid, string Marker, bool Sent, string[] Evidence);

public sealed record SpotMissingTargetReq(string SpotRid);

public sealed record SpotMissingTargetReply(string SpotRid, bool Failed);

public sealed record StageProbeReq(string Marker, int Delta);

public sealed record StageTimerStartCommand(string Name, int PeriodMs);

public sealed record SpotStageProbeReq(string SpotRid, string Marker, int Delta);

public sealed record SpotStageTimerReq(string SpotRid, string Name, int PeriodMs);

public sealed record SpotStageTimerReply(string SpotRid, string Name, bool Started, string[] Evidence);

public sealed record SpotEvent(string Marker);

public sealed record SpotOutboundReq(string Marker);

public sealed record SpotOutboundNegativeReq(string Marker);

public sealed record SpotOutboundRouteReq(string SpotRid, string Marker);

public sealed record SpotOutboundRouteReply(string SpotRid, string Marker, bool Accepted, string[] Evidence);

public sealed record ChannelEchoReq(string Value);

public sealed record ChannelEchoReply(string Value);

public sealed record ChannelNotify(string Marker);

public sealed record CreateSpotReq(string SpotRid);

public sealed record CreateSpotReply(string SpotRid, string NodeRid, string State);

public sealed record CloseSpotReq(string SpotRid);

public sealed record CloseSpotReply(string SpotRid, bool Closed);

public sealed record JoinUserSpotActorReq(string SpotRid, string ActorId);

public sealed record JoinUserSpotActorReply(string SpotRid, string ActorId, bool Accepted, ulong Generation);

public sealed record ActorPingReq(string Value);

public sealed record SlowActorPingReq(string Value, int DelayMs);

public sealed record ActorPingReply(string ActorId, string NodeRid, string SpotRid, string Value, int Seen);

public sealed record ActorPushReq(string Value);

public sealed record ActorPushNotify(string ActorId, string Value, int Seen);

public sealed record ComplexActorReq(
    string DisplayName,
    int Level,
    string[] Tags,
    Dictionary<string, string> Attributes);

public sealed record ComplexActorReply(
    string ActorId,
    string DisplayName,
    int Level,
    string[] Tags,
    Dictionary<string, string> Attributes);

public sealed record AuthReq(string ActorId, string DisplayName, string NodeRid);

public sealed record UserSpotAuthReq(string SpotRid, string ActorId, string DisplayName, string NodeRid);

public sealed record AuthReply(string ActorId, string NodeRid);

public sealed record MultiBindReq(string FirstActorId, string SecondActorId, string NodeRid);

public sealed record MultiBindReply(int BoundCount);

public sealed record LeaveReq(string ActorId);

public sealed record LeaveReply(string ActorId, bool Accepted);

public sealed record SnapshotReq(string ActorId);

public sealed record SnapshotReply(string ActorId, int Seen);

public sealed record ControlPingReq(string Value);

public sealed record ControlPingReply(string Value, string NodeRid);

public sealed record SpotTypeMismatchReq(string SpotRid);

public sealed record SpotTypeMismatchReply(string SpotRid, bool Failed, string ErrorKind, string State);

public sealed record WorkerStartReq(string Marker, int DelayMs);

public sealed record WorkerStartReply(string SpotRid, string NodeRid, string Marker);

public sealed record SpotWorkerStartReq(string SpotRid, string Marker, int DelayMs);

public sealed record SpotWorkerCompleteReq(string SpotRid, string Marker);

public sealed record SpotWorkerCompleteReply(string SpotRid, string Marker, bool Completed, string[] Evidence);

public sealed record SlowSpotReq(string Marker, int DelayMs);

public sealed record SlowSpotReply(string SpotRid, string NodeRid, string Marker);

public sealed record SpotSlowRouteReq(string SpotRid, string Marker, int DelayMs, int TimeoutMs);

public sealed record SpotSlowRouteReply(string SpotRid, string Marker, bool TimedOut);

public sealed record DestroyActorReq(string ActorId);

public sealed record DestroyActorReply(string ActorId, bool Destroyed);

public sealed record OverrunStartCommand(string Name, string Policy, int PeriodMs);

public sealed record SpotOverrunStartReq(string SpotRid, string Name, string Policy, int PeriodMs);

public sealed record SpotOverrunStartReply(string SpotRid, string Name, string Policy, bool Started, string[] Evidence);

public sealed record TimerStartCommand(string Name, int PeriodMs);

public sealed record IdleCloseCommand(string Name, int PeriodMs);

public sealed record SpotTimerStartReq(string SpotRid, string Name, int PeriodMs);

public sealed record SpotTimerStartReply(string SpotRid, string Name, bool Started, string[] Evidence);

public sealed record SpotIdleCloseReq(string SpotRid, string Name, int PeriodMs);

public sealed record SpotIdleCloseReply(string SpotRid, string Name, bool Closed, string[] Evidence);

public sealed record SpotToSpotReq(string TargetSpotRid, string Marker);

public sealed record SpotToSpotReply(string SourceSpotRid, string TargetSpotRid, int TargetValue);

public sealed record SpotToSpotRouteReq(string SourceSpotRid, string TargetSpotRid, string Marker);

public sealed record SpotToSpotTimeoutReq(string TargetSpotRid, string Marker);

public sealed record SpotToSpotTimeoutReply(string SourceSpotRid, string TargetSpotRid, bool Failed);

public sealed record SpotToSpotTimeoutRouteReq(string SourceSpotRid, string TargetSpotRid, string Marker);

public sealed record SpotToSpotNegativeReq(string TargetSpotRid, string Marker);

public sealed record SpotToSpotNegativeReply(string SourceSpotRid, string TargetSpotRid, bool RequestFailed);

public sealed record SpotToSpotNegativeRouteReq(string SourceSpotRid, string TargetSpotRid, string Marker);

public sealed record MultiNodeCreateSpotReq(string SpotRid, int Delta);

public sealed record MultiNodeCreateSpotReply(string SpotRid, string NodeRid, string State, int Value);

public sealed record MultiNodeStateRouteReq(string SpotRid, int Delta);

public sealed record SpotPublishReq(string SpotRid, string Marker);

public sealed record SpotPublishReply(
    string Operation,
    string PublisherRid,
    string SpotRid,
    string Marker,
    string[] Evidence);

public sealed record SpotPublishObserveReply(
    string Operation,
    string SpotRid,
    string Marker,
    bool Received,
    string[] Evidence);