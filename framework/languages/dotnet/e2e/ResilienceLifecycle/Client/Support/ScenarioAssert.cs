namespace ResilienceLifecycle.Client;

internal static class ScenarioAssert
{
    public static void That(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    public static string[] AllProviderEvidence(LifecycleApiResult result) =>
        result.ProviderAEvidence.Concat(result.ProviderBEvidence).ToArray();
}
