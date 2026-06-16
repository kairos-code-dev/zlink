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
            var spot = mesh.AddNode(SampleNames.TrackingSpotNode);
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
