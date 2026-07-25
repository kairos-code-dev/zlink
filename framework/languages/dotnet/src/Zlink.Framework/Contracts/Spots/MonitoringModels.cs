namespace Zlink.Framework.Contracts.Spots;

public enum ZLinkSpotNodeState
{
    Idle = 1,
    Connecting = 2,
    PartialReady = 3,
    Ready = 4,
    Error = 5
}

public enum ZLinkSpotPeerSource
{
    Manual = 1,
    Discovery = 2,
    Mixed = 3
}

public enum ZLinkSpotPeerKind
{
    SpotMesh = 1,
    RouterChannel = 2
}

public enum ZLinkSpotPeerState
{
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

public enum ZLinkSubjectKind : uint
{
    None = 0,
    Topic = 1,
    Pattern = 2
}

public enum ZLinkSpotRole
{
    Pub = 1,
    Sub = 2
}

public sealed record ZLinkSpotNodeStatus(
    string ChannelName,
    string LocalEndpoint,
    RoutingId? NodeRoutingId,
    ZLinkSpotNodeState State,
    uint ConfiguredPeerCount,
    uint ActivePeerCount,
    uint ConnectedPeerCount,
    uint SubjectCount,
    uint ReadySubjectCount,
    int LastError,
    ulong LastChangedMs);

public sealed record ZLinkSpotNodePeerEntry(
    string ChannelName,
    string LocalEndpoint,
    string PeerEndpoint,
    ZLinkSpotPeerSource Source,
    ZLinkSpotPeerKind Kind,
    ZLinkSpotPeerState State,
    int Weight,
    ulong ConnectedSinceMs,
    ulong LastChangedMs);

public sealed record ZLinkSpotNodeSubjectEntry(
    ZLinkSpotRole Role,
    string Subject,
    ZLinkSubjectKind SubjectKind,
    uint ReadyPeerCount,
    uint ActivePeerCount,
    ulong LastChangedMs);
