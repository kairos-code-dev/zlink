namespace DiscoveryRegistryHa.Shared;

public static class DiscoveryRegistryHaNames
{
    public const string Channel = "discovery.registry.ha.profile";
}

public sealed record ProfileRequest(string Value, string Marker);

public sealed record ProfileReply(string Value, string ProviderRid, string Marker);

public sealed record EvidenceWaitRequest(string Contains, int TimeoutMilliseconds = 10000);

public sealed record TopologyReadyWaitRequest(int ReadyCount, int TimeoutMilliseconds = 10000);

public sealed record MemberEndpointWaitRequest(string Endpoint, int TimeoutMilliseconds = 10000);