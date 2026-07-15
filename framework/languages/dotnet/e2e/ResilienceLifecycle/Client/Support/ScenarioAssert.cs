namespace ResilienceLifecycle.Client.Support;

internal static class ScenarioAssert
{
    public static string[] AllProviderEvidence(LifecycleApiRes result)
    {
        return result.ProviderAEvidence.Concat(result.ProviderBEvidence).ToArray();
    }
}
