using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.DispatchCenter;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
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
    options.ConfigureDispatch()
        .SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLogFile(SampleFlowLog.Path("dispatch-center"))
        .TraceNodeId("dispatch-center");
    options.AddHandlersFromAssemblyOf(typeof(AssignDeliveryHandler));
    options.Codecs.AddJson();
    options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
    {
        var channel = options.AddClientServerChannel(SampleNames.DispatchRouteChannel);
        channel.EnableServer(topology.DispatchCenterRouteEndpoint);
        channel.AddRequestHandler<AssignDeliveryHandler, AssignDelivery, AssignDeliveryResult>();

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.CourierAChannel);
        channel.EnableClient();

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.CourierBChannel);
        channel.EnableClient();

    }
    {
        var channel = options.AddClientServerChannel(SampleNames.TrackingRouteChannel);
        channel.EnableClient();

    }
});

await builder.Build().RunAsync();
