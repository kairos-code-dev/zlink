namespace YieldDispatch.Shared;

public static class YieldDispatchNames
{
    public const string ControlChannel = "yield.control";
    public const string DelayChannel = "yield.delay";
    public const string SpotChannel = "yield.spot";
    public const string SpotRouteChannel = "yield.spot.route";
    public const string StreamNode = "yield.stream";
    public const string ActorType = "yield.actor";
    public const string ActorIdMetadata = "actor-id";
    public const string SpotRidMetadata = "spot-rid";
    public const string TargetNodeRidMetadata = "target-node-rid";
}

public sealed record YieldShutdownScenarioReq(string RequestId, string SpotRid, int DelayMs);

public sealed record YieldShutdownRecoveryReq(string RequestId, string SpotRid);

public sealed record BindYieldActorsReq(string SpotRid, string[] ActorIds);

public sealed record BindYieldActorsReply(string SpotRid, YieldActorBinding[] Actors);

public sealed record YieldActorBinding(string ActorId, string NodeRid, ulong Generation);

public sealed record YieldEvidenceReq(string RequestId);

public sealed record YieldEvidenceReply(string RequestId, string[] Evidence);

public sealed record YieldEvidenceWaitReq(string RequestId, string Marker, int TimeoutMilliseconds = 20000);

public sealed record DelayReq(string RequestId, int DelayMs, string Marker);

public sealed record DelayReply(string RequestId, string Marker, string NodeRid);

public sealed record HoldReq(string RequestId, int DelayMs);

public sealed record HoldCommand(string RequestId, int DelayMs);

public sealed record YieldReq(string RequestId, int DelayMs, string CorrelationId);

public sealed record YieldCommand(string RequestId, int DelayMs, string CorrelationId);

public sealed record WorkerYieldReq(string RequestId, int DelayMs);

public sealed record WorkerYieldCommand(string RequestId, int DelayMs);

public sealed record YieldTimeoutReq(string RequestId, int DelayMs, int TimeoutMs);

public sealed record YieldTimeoutCommand(string RequestId, int DelayMs, int TimeoutMs);

public sealed record YieldCancelReq(string RequestId, int DelayMs, int CancelAfterMs);

public sealed record YieldCancelCommand(string RequestId, int DelayMs, int CancelAfterMs);

public sealed record RemoteSpotYieldReq(string RequestId, string TargetSpotRid, int DelayMs);

public sealed record RemoteSpotYieldCommand(string RequestId, string TargetSpotRid, int DelayMs);

public sealed record EnsureSpotReq(string SpotRid);

public sealed record EnsureSpotReply(string SpotRid, string NodeRid);

public sealed record ProbeReq(string RequestId, string Marker);

public sealed record ProbeCommand(string RequestId, string Marker);

public sealed record TimerStartReq(string RequestId, string TimerName, string Mode, int PeriodMs, int DelayMs);

public sealed record TimerStartCommand(string RequestId, string TimerName, string Mode, int PeriodMs, int DelayMs);

public sealed record TimerStopReq(string RequestId);

public sealed record TimerStopCommand(string RequestId);

public sealed record ActorYieldReq(string RequestId, int DelayMs);

public sealed record ActorFastReq(string RequestId, string Marker);

public sealed record ActorJoinYieldReq(string RequestId, string TargetSpotRid);

public sealed record ActorPushYieldReq(string RequestId, int DelayMs, string Value);

public sealed record ActorPushNotify(string ActorId, string RequestId, string Value, string NodeRid);

public sealed record ActorYieldReply(
    string ScenarioId,
    string RequestId,
    string ActorId,
    string SpotRid,
    string NodeRid,
    string Marker);

public sealed record YieldDispatchReply(
    string ScenarioId,
    string RequestId,
    string SpotRid,
    string NodeRid,
    string Marker);

public sealed record YieldTimeoutReply(
    string ScenarioId,
    string RequestId,
    string SpotRid,
    string NodeRid,
    bool TimedOut,
    string Error);

public sealed record YieldCancelReply(
    string ScenarioId,
    string RequestId,
    string SpotRid,
    string NodeRid,
    bool Canceled,
    string Error);

public sealed record YieldScenarioResult(
    string Operation,
    string SpotRid,
    string[] Evidence);
