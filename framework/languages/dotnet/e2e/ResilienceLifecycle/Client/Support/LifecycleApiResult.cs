namespace ResilienceLifecycle.Client.Support;

internal sealed record LifecycleApiResult(
    string Operation,
    string[] ProviderAEvidence,
    string[] ProviderBEvidence);
