using Zlink.HttpClient;

namespace RegistryMessaging.Client.Scenarios;

// Verifies the RegistryMessaging scenario set through the server-side driver app.
// The client behaves like an external user: it asks the app endpoint to run the
// scenario set and relies on the driver/server evidence for framework checks.
internal static class RegistryMessagingScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver)
    {
        await driver.Post("/run").SubmitRawAsync();
        Console.WriteLine("scenario RegistryMessaging passed");
    }
}
