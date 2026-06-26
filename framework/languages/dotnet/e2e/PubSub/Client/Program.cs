using PubSub.Client;
using PubSub.Client.Scenarios;
using System.Diagnostics;
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
    await FanoutBasicDeliveryScenario.RunAsync(publisher, subscribers);
    await TopicFilterScenario.RunAsync(publisher, subscribers);
    await LateSubscriberScenario.RunAsync(
        publisher,
        lateSubscriber,
        processLauncher,
        options.LateSubscriberUrl);
    await SubscriberReconnectScenario.RunAsync(
        publisher,
        lateSubscriber,
        subscribers.Take(2).ToArray(),
        processLauncher,
        options.LateSubscriberUrl);
    await SlowSubscriberScenario.RunAsync(publisher, subscribers.Take(2).ToArray(), subscribers[^1]);
    restartedPublisher = await PublisherRestartScenario.RunAsync(publisher, subscribers, processLauncher);
    await MissingMessageNameScenario.RunAsync(publisher, subscribers);
}
finally
{
    if (restartedPublisher is { HasExited: false })
    {
        await publisher.Post("/shutdown").SubmitRawAsync();
        await restartedPublisher.WaitForExitAsync();
    }

    foreach (var subscriber in subscribers)
    {
        subscriber.Dispose();
    }
}

Console.WriteLine("pubsub e2e result=passed");
