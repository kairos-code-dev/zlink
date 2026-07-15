using DeliveryDispatch.Client;
using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;
using Zlink.Samples.Logging;

var configuration = SampleConfiguration.Load(args);
using var loggerFactory = SampleLogging.CreateFactory(configuration.Role.LogDir, "client");
var logger = loggerFactory.CreateLogger("DeliveryDispatch.Client");

using var http = ZLinkHttpClient.Create(configuration.Topology.DispatchHttpUrl).Build();
await using var customer = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(configuration.Topology.CustomerStreamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});
await using var courierA = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(configuration.Topology.CourierStreamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});
await using var courierB = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri(configuration.Topology.CourierStreamEndpoint),
    ConnectTimeout = TimeSpan.FromSeconds(5),
    RequestTimeout = TimeSpan.FromSeconds(5),
    WaitTimeout = TimeSpan.FromSeconds(15),
    DispatchMode = ZlinkStreamDispatchMode.Immediate
});

await new DeliveryDispatchClientScenario(logger).RunAsync(http, customer, courierA, courierB);

logger.LogInformation("deliverydispatch=completed");
