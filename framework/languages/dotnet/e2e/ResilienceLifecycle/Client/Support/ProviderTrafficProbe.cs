using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Support;

/// <summary>
/// Drives consumer traffic until a specific provider proves it received it.
/// A store row appearing (topology/wait) precedes the consumer's own
/// reconcile dial by up to a poll tick, so a fixed batch fired right after
/// the row shows up can land entirely on the surviving provider.
/// </summary>
internal static class ProviderTrafficProbe
{
    public static async Task DriveUntilProviderServesAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient provider,
        string markerPrefix,
        string scenario,
        string? evidencePattern = null)
    {
        var pattern = evidencePattern ?? $"marker={markerPrefix}-";
        for (var round = 0; round < 300; round++)
        {
            var marker = $"{markerPrefix}-{round}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", marker))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(
                reply.Value == "profile:fast",
                $"{scenario} request returned an unexpected value.");

            try
            {
                var snapshot = (await provider.Get("/evidence").SubmitAsync<string[]>()).Body;
                if (snapshot.Any(entry => entry.Contains(pattern, StringComparison.Ordinal))) return;
            }
            catch
            {
                // The provider may still be settling after a stop/start cycle;
                // keep sending while its dealer link finishes reconnecting.
            }

            await Task.Delay(100);
        }

        throw new InvalidOperationException(
            $"{scenario}: provider did not receive '{markerPrefix}' traffic before the probe deadline.");
    }
}
