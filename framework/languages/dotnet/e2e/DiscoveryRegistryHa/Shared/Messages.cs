namespace DiscoveryRegistryHa.Shared;

public static class DiscoveryRegistryHaNames
{
    public const string Channel = "discovery.registry.ha.profile";
}

public sealed record ProfileRequest(string Value, string Marker);

public sealed record ProfileReply(string Value, string ProviderRid, string Marker);
