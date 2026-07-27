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
