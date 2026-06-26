using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// Verifies the resilience/lifecycle scenario set through the server-side driver app.
// The client only calls the application endpoint, while the driver performs the
// framework requests, restarts, and evidence checks inside the server boundary.
internal static class ResilienceLifecycleScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver)
    {
        await driver.Post("/run").SubmitRawAsync();
        Console.WriteLine("scenario ResilienceLifecycle passed");
    }
}
