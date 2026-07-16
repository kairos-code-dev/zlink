namespace RuntimeMonitoring.Shared;

public static class RuntimeMonitoringNames
{
    public const string Channel = "monitor.profile";
    public const string LocationRuntimeSource = "location-runtime";
    public const string ChannelServerSource = "monitor.profile.server";
    public const string ChannelClientSource = "monitor.profile.client";
    public const string SpotChannel = "monitor.spot";
    public const string SpotNode = SpotChannel;
}

public sealed record ProfileReq(string Value, string Marker);

public sealed record ProfileRes(string Value, string ProviderRid, string Marker);

public sealed record DrainResultRes(string Result, string? Reason = null);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000,
    int AfterIndex = 0);
