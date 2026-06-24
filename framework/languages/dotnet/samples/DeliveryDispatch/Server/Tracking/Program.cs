using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Tracking;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

var topology = SampleTopology.Create();
var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddSingleton(topology);
builder.Services.AddSingleton<EvidenceStore>();
builder.Services.AddSingleton<DeliverySpotDirectory>();
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch()
        .SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLogFile(SampleFlowLog.Path("tracking"))
        .TraceNodeId("tracking");
    options.AddHandlersFromAssemblyOf(typeof(EnsureCustomerActorHandler));
    options.Codecs.AddJson();
    options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
    options.AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
    {
        var channel = options.AddClientServerChannel(SampleNames.TrackingRouteChannel);
        channel.EnableServer(topology.TrackingRouteEndpoint);
        channel.AddRequestHandler<EnsureCustomerActorHandler, EnsureCustomerActor, CustomerActorEnsured>();
        channel.AddRequestHandler<SubscribeCustomerToDeliveryHandler, SubscribeCustomerToDelivery, CustomerDeliverySubscribed>();
        channel.AddRequestHandler<DeliveryStatusChangedHandler, DeliveryStatusChanged, DeliveryStatusAck>();

    }
    {
        var channel = options.AddFanoutChannel(SampleNames.StatusFanoutChannel);
        channel.EnablePublisher(topology.StatusFanoutEndpoint);

    }
    {
        var mesh = options.AddSpotMesh(SampleNames.DeliverySpotDiscovery);
        mesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
        {
            var spot = mesh;
            {
                var router = spot.EnableRouter(topology.TrackingSpotRouterEndpoint);
                router.SetRouterRoutingId(topology.TrackingSpotNodeRid);

            }
            spot.EnablePubSub(topology.TrackingSpotEndpoint);
            spot.AddEntrySpot<CustomerEntrySpot>();
            spot.AddSpotFactory<DeliveryTrackingSpot>();

        }

    }
});

await builder.Build().RunAsync();
