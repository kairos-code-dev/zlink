namespace SpotService.Shared;

public static class SpotServiceNames
{
    public const string SpotChannel = "spot.service";
    public const string ControlChannel = "spot.control";
    public const string ExternalSpotChannel = "spot.external.play-a";
    public const string ExternalClientChannel = "spot.external.client";
    public const string ExternalClientServerChannel = "spot.external.cs.client";
    public const string StreamNode = "session-stream";
    public const string PlaySpotNode = "play-node";
    public const string SessionSpotNode = "session-node";
    public const string EdgeSpotNode = "edge-node";
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

public sealed record CreateSpotReq(string SpotRid);

public sealed record CreateSpotReply(string SpotRid, string NodeRid, string State);

public sealed record ActorPingReq(string Value);

public sealed record ActorPingReply(string ActorId, string NodeRid, string SpotRid, string Value, int Seen);

public sealed record ActorPushReq(string Value);

public sealed record ActorPushNotify(string ActorId, string Value, int Seen);

public sealed record AuthReq(string ActorId, string DisplayName, string NodeRid);

public sealed record AuthReply(string ActorId, string NodeRid);

public sealed record MultiBindReq(string FirstActorId, string SecondActorId, string NodeRid);

public sealed record MultiBindReply(int BoundCount);

public sealed record LeaveReq(string ActorId);

public sealed record LeaveReply(string ActorId, bool Accepted);

public sealed record SnapshotReq(string ActorId);

public sealed record SnapshotReply(string ActorId, int Seen);

public sealed record ControlPingReq(string Value);

public sealed record ControlPingReply(string Value, string NodeRid);
