using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Tracking;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink.Codecs.Json;
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
    options.DefaultTimeout = SampleTimings.FrameworkTimeout;
    options.AddHandlersFromAssemblyOf(typeof(EnsureCustomerActorHandler));
    options.Codecs.AddJson();
    options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
    options.AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
    options.AddClientServerChannel(SampleNames.TrackingRouteChannel, channel =>
    {
        channel.EnableServer(server => server.Bind(topology.TrackingRouteEndpoint));
        channel.AddRequestHandler<EnsureCustomerActorHandler, EnsureCustomerActor, CustomerActorEnsured>();
        channel.AddRequestHandler<SubscribeCustomerToDeliveryHandler, SubscribeCustomerToDelivery, CustomerDeliverySubscribed>();
        channel.AddRequestHandler<DeliveryStatusChangedHandler, DeliveryStatusChanged, DeliveryStatusAck>();
    });
    options.AddFanoutChannel(SampleNames.StatusFanoutChannel, channel =>
    {
        channel.EnablePublisher(publisher => publisher.Bind(topology.StatusFanoutEndpoint));
    });
    options.AddSpotMesh(SampleNames.DeliverySpotDiscovery, mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
        mesh.AddNode(SampleNames.TrackingSpotNode, spot =>
        {
            spot.EnableRouter(router =>
            {
                router.BindRouter(topology.TrackingSpotRouterEndpoint);
                router.SetRoutingId(topology.TrackingSpotNodeRid);
            });
            spot.EnablePubSub(pubsub => pubsub.BindPubSub(topology.TrackingSpotEndpoint));
            spot.AddEntrySpot<CustomerEntrySpot>();
            spot.AddSpotFactory<DeliveryTrackingSpot>();
        });
    });
});

await builder.Build().RunAsync();
