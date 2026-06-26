using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies one SpotService scenario set through the server-side driver app.
// The client uses the application HTTP endpoint and leaves framework, stream,
// and process-boundary checks inside the driver/server side of the e2e app.
internal static class SpotServiceScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver, string scenarioSet)
    {
        await driver.Post($"/run/{Uri.EscapeDataString(scenarioSet)}").SubmitRawAsync();
        Console.WriteLine($"scenario SpotService.{scenarioSet} passed");
    }
}
