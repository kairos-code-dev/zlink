using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Tracking.Spots.DeliveryTrackingSpot;
using DeliveryDispatch.Server.Tracking.Spots.EntrySpot;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.Tracking;

public static class TrackingServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddSingleton<DeliverySpotDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("tracking"))
                .TraceLabel("tracking");
            options.AddHandlersFromAssemblyOf(typeof(EnsureCustomerActorHandler));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableServer(topology.TrackingRouteEndpoint)
                .AddRequestHandler<EnsureCustomerActorHandler, EnsureCustomerActor, CustomerActorEnsured>()
                .AddRequestHandler<SubscribeCustomerToDeliveryHandler, SubscribeCustomerToDelivery, CustomerDeliverySubscribed>()
                .AddRequestHandler<DeliveryStatusChangedHandler, DeliveryStatusChanged, DeliveryStatusAck>();
            options.AddFanoutChannel(SampleNames.StatusFanoutChannel)
                .EnablePublisher(topology.StatusFanoutEndpoint);
            var mesh = options.AddSpotMesh(SampleNames.DeliverySpotDiscovery);
            mesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            mesh.EnableRouter(topology.TrackingSpotRouterEndpoint)
                .SetRoutingId(topology.TrackingSpotNodeRid);
            mesh.EnablePubSub(topology.TrackingSpotEndpoint);
            mesh.AddEntrySpot<CustomerEntrySpot>();
            mesh.AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
            mesh.AddSpotFactory<DeliveryTrackingSpot>();
        });

        return builder.Build();
    }
}
