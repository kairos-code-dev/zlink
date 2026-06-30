using System.Diagnostics;
using PubSub.Client.Scenarios;
using PubSub.Client.Support;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var publisher = ZLinkHttpClient.Create(options.PublisherUrl).Json().Build();
using var lateSubscriber = ZLinkHttpClient.Create(options.LateSubscriberUrl).Json().Build();
var subscribers = options.SubscriberUrls
    .Select(url => ZLinkHttpClient.Create(url).Json().Build())
    .ToArray();
var processLauncher = new ServerProcessLauncher(options);
Process? restartedPublisher = null;

try
{
    var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
    {
        ["PS-A1"] = () => FanoutBasicDeliveryScenario.RunAsync(publisher, subscribers),
        ["PS-A2"] = () => TopicFilterScenario.RunAsync(publisher, subscribers),
        ["PS-A3"] = () => LateSubscriberScenario.RunAsync(
            publisher,
            lateSubscriber,
            processLauncher,
            options.LateSubscriberUrl),
        ["PS-A4"] = () => SubscriberReconnectScenario.RunAsync(
            publisher,
            lateSubscriber,
            subscribers.Take(2).ToArray(),
            processLauncher,
            options.LateSubscriberUrl),
        ["PS-B1"] = () => SlowSubscriberScenario.RunAsync(publisher, subscribers.Take(2).ToArray(), subscribers[^1]),
        ["PS-B2"] = async () => restartedPublisher = await PublisherRestartScenario.RunAsync(
            publisher,
            subscribers,
            processLauncher),
        ["PS-C1"] = () => MissingMessageNameScenario.RunAsync(publisher, subscribers)
    };

    if (string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase))
        foreach (var scenario in scenarios.Values)
            await scenario();
    else if (scenarios.TryGetValue(options.Scenario, out var selected))
        await selected();
    else
        throw new ArgumentException($"Unknown scenario '{options.Scenario}'.");
}
finally
{
    if (restartedPublisher is { HasExited: false })
    {
        await publisher.Post("/shutdown").SubmitRawAsync();
        await restartedPublisher.WaitForExitAsync();
    }

    foreach (var subscriber in subscribers) subscriber.Dispose();
}

Console.WriteLine("pubsub e2e result=passed");