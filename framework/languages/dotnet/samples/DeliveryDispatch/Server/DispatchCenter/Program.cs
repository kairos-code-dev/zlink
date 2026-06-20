using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.DispatchCenter;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
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
    options.ConfigureDispatch().SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>();
    options.AddHandlersFromAssemblyOf(typeof(AssignDeliveryHandler));
    options.Codecs.AddJson();
    {
        var channel = options.AddClientServerChannel(SampleNames.DispatchRouteChannel);
        channel.EnableServer(topology.DispatchCenterRouteEndpoint);
        channel.AddRequestHandler<AssignDeliveryHandler, AssignDelivery, AssignDeliveryResult>();

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.CourierAChannel);
                channel.EnableClient(topology.CourierAEndpoint);

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.CourierBChannel);
                channel.EnableClient(topology.CourierBEndpoint);

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.TrackingRouteChannel);
                channel.EnableClient(topology.TrackingRouteEndpoint);

    }
});

await builder.Build().RunAsync();
