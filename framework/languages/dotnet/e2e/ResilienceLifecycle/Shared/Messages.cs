namespace ResilienceLifecycle.Shared;

public static class ResilienceLifecycleNames
{
    public const string Channel = "resilience.profile";
}

public sealed record ProfileRequest(string Value, string Marker);

public sealed record ProfileReply(string Value, string ProviderRid, string Marker);

public sealed record ProfileCommand(string Marker);
