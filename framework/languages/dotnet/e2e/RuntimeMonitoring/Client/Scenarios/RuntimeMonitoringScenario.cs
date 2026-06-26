using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

// Verifies socket, registry, spot, dispatch, and lifecycle monitoring through
// the server-side driver app. The client observes the application endpoint only.
internal static class RuntimeMonitoringScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver)
    {
        await driver.Post("/run").SubmitRawAsync();
        Console.WriteLine("scenario RuntimeMonitoring passed");
    }
}
