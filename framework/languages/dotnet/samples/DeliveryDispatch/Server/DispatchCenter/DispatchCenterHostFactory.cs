using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.DispatchCenter;

public static class DispatchCenterHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        Console.Error.WriteLine(
            $"deliverydispatch dispatch: topology tracking={topology.TrackingRouteEndpoint} courierA={topology.CourierAEndpoint} courierB={topology.CourierBEndpoint}");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<DispatchWorkQueue>();
        builder.Services.AddHostedService<DispatchWorker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("dispatch-center"))
                .TraceLabel("dispatch-center");
            options.AddHandlersFromAssemblyOf(typeof(AssignDeliveryHandler));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.DispatchRouteChannel)
                .EnableServer(topology.DispatchCenterRouteEndpoint)
                .AddRequestHandler<AssignDeliveryHandler, AssignDelivery, AssignDeliveryResult>();
            options.AddClientServerChannel(SampleNames.CourierAChannel)
                .EnableClient();
            options.AddClientServerChannel(SampleNames.CourierBChannel)
                .EnableClient();
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient();
        });

        return builder.Build();
    }
}
