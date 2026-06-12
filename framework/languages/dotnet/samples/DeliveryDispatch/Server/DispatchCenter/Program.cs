using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.DispatchCenter;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

var topology = SampleTopology.Create();
var builder = Host.CreateApplicationBuilder(args);
Console.Error.WriteLine(
    $"deliverydispatch dispatch: topology tracking={topology.TrackingRouteEndpoint} courierA={topology.CourierAEndpoint} courierB={topology.CourierBEndpoint}");
builder.Services.AddSingleton(topology);
builder.Services.AddSingleton<DispatchWorkQueue>();
builder.Services.AddHostedService<DispatchWorker>();
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = SampleTimings.FrameworkTimeout;
    options.AddHandlersFromAssemblyOf(typeof(AssignDeliveryHandler));
    options.Codecs.AddJson();
    options.AddClientServerChannel(SampleNames.DispatchRouteChannel, channel =>
    {
        channel.EnableServer(server => server.Bind(topology.DispatchCenterRouteEndpoint));
        channel.AddRequestHandler<AssignDeliveryHandler, AssignDelivery, AssignDeliveryResult>();
    });
    options.AddClientServerChannel(SampleNames.CourierAChannel, channel =>
    {
        channel.EnableClient(client => client.UseManualConnections(
            connections => connections.Connect(topology.CourierAEndpoint)));
    });
    options.AddClientServerChannel(SampleNames.CourierBChannel, channel =>
    {
        channel.EnableClient(client => client.UseManualConnections(
            connections => connections.Connect(topology.CourierBEndpoint)));
    });
    options.AddClientServerChannel(SampleNames.TrackingRouteChannel, channel =>
    {
        channel.EnableClient(client => client.UseManualConnections(
            connections => connections.Connect(topology.TrackingRouteEndpoint)));
    });
});

await builder.Build().RunAsync();
