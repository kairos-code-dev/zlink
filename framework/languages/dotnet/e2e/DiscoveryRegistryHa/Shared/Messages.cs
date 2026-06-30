namespace DiscoveryRegistryHa.Shared;

public static class DiscoveryRegistryHaNames
{
    public const string Channel = "discovery.registry.ha.profile";
}

public sealed record ProfileReq(string Value, string Marker);

public sealed record ProfileRes(string Value, string ProviderRid, string Marker);

public sealed record EvidenceWaitReq(string Contains, int TimeoutMilliseconds = 10000);

public sealed record TopologyReadyWaitReq(int ReadyCount, int TimeoutMilliseconds = 10000);

public sealed record MemberEndpointWaitReq(string Endpoint, int TimeoutMilliseconds = 10000);