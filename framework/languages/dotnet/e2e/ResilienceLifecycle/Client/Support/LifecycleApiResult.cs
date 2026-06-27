namespace ResilienceLifecycle.Client;

internal sealed record LifecycleApiResult(
    string Operation,
    string[] ProviderAEvidence,
    string[] ProviderBEvidence);
