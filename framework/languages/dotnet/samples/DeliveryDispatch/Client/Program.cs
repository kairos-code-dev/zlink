using DeliveryDispatch.Client;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;
using Zlink.Samples.Logging;

var apiUrl = ReadOption(args, "--api-url")
             ?? throw new ArgumentException("Missing --api-url.");
var streamEndpoint = ReadOption(args, "--stream-endpoint")
                     ?? throw new ArgumentException("Missing --stream-endpoint.");
var courierStreamEndpoint = ReadOption(args, "--courier-stream-endpoint")
                            ?? throw new ArgumentException("Missing --courier-stream-endpoint.");
var logDir = ReadOption(args, "--log-dir")
             ?? throw new ArgumentException("Missing --log-dir.");
using var loggerFactory = SampleLogging.CreateFactory(logDir, "client");
var logger = loggerFactory.CreateLogger("DeliveryDispatch.Client");

using var http = ZLinkHttpClient.Create(apiUrl).Build();
await using var customer = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(streamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});
await using var courierA = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(courierStreamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});
await using var courierB = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(courierStreamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});

await new DeliveryDispatchClientScenario(logger).RunAsync(http, customer, courierA, courierB);

logger.LogInformation("deliverydispatch=completed");

static string? ReadOption(string[] args, string name)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
}
