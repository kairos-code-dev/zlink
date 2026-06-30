using DeliveryDispatch.Client;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

var apiUrl = ReadOption(args, "--api-url")
             ?? throw new ArgumentException("Missing --api-url.");
var streamEndpoint = ReadOption(args, "--stream-endpoint")
                     ?? throw new ArgumentException("Missing --stream-endpoint.");
var courierStreamEndpoint = ReadOption(args, "--courier-stream-endpoint")
                            ?? throw new ArgumentException("Missing --courier-stream-endpoint.");

using var http = ZLinkHttpClient.Create(apiUrl).Json().Build();
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

await new DeliveryDispatchClientScenario().RunAsync(http, customer, courierA, courierB);

Console.WriteLine("deliverydispatch=completed");

static string? ReadOption(string[] args, string name)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
}