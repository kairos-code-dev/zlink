namespace Zlink.Framework.Contracts.Eventing;

public static class ZLinkMeters
{
    public const string Framework = "zlink.framework";
}

public enum ZLinkFlowOrigin : byte
{
    Inbound = 1,
    Timer = 2,
    Application = 3,
    Lifecycle = 4
}

public enum ZLinkDrainState
{
    Serving = 0,
    Draining = 1,
    Drained = 2,
    ForceStopping = 3
}

public sealed record ZLinkDrainEvent(
    DateTimeOffset Timestamp,
    ZLinkDrainState State) : IZLinkRuntimeEvent
{
    public string SourceName => "drain";
}
