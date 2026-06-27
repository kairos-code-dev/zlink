namespace RuntimeMonitoring.Shared;

public static class RuntimeMonitoringNames
{
    public const string Channel = "monitor.profile";
    public const string ChannelServerSource = "monitor.profile.server";
    public const string ChannelClientSource = "monitor.profile.client";
    public const string SpotChannel = "monitor.spot";
    public const string SpotNode = SpotChannel;
}

public sealed record ProfileRequest(string Value, string Marker);

public sealed record ProfileReply(string Value, string ProviderRid, string Marker);

public sealed record EvidenceWaitRequest(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000);
