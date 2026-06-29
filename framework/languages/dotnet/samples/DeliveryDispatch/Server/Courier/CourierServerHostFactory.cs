using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.Courier;

public static class CourierServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        string courierId,
        string mode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(new CourierOptions(courierId, mode));
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(courierId))
                .TraceLabel(courierId);
            options.AddHandlersFromAssemblyOf(typeof(OfferDeliveryHandler));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.CourierChannel(courierId))
                .EnableServer(topology.CourierEndpoint(courierId))
                .AddRequestHandler<OfferDeliveryHandler, OfferDelivery, OfferDeliveryResult>();
        });

        return builder.Build();
    }
}
