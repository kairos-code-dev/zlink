using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Session;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

var topology = SampleTopology.Create();
var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddSingleton(topology);
builder.Services.AddSingleton<CustomerSessionDirectory>();
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = SampleTimings.FrameworkTimeout;
    options.AddHandlersFromAssemblyOf(typeof(CustomerSession));
    options.Codecs.AddJson();
    options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
    options.AddClientServerChannel(SampleNames.TrackingRouteChannel, channel =>
    {
        channel.EnableClient();
    });
    options.AddFanoutChannel(SampleNames.StatusFanoutChannel, channel =>
    {
        channel.EnableSubscriber(subscriber => subscriber.UseManualConnections(
            connections => connections.Connect(topology.StatusFanoutEndpoint)));
        channel.AddPublishHandler<DeliveryStatusFanoutHandler, DeliveryStatusNotify>();
    });
    options.AddSpotMesh(SampleNames.DeliverySpotDiscovery, mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
        mesh.AddNode(SampleNames.SessionSpotNode, spot =>
        {
            spot.EnableRouter(router =>
            {
                router.BindRouter(topology.SessionSpotRouterEndpoint);
                router.SetRoutingId(topology.SessionSpotNodeRid);
            });
            spot.EnablePubSub(pubsub =>
            {
                pubsub.BindPubSub(topology.SessionSpotEndpoint);
                pubsub.SetRoutingId(topology.SessionSpotPubRid);
            });
        });
    });
    options.AddStreamNode(SampleNames.CustomerStreamNode, stream =>
    {
        stream.AttachActorGateway(SampleNames.SessionSpotNode);
        stream.Bind(topology.SessionStreamEndpoint);
        stream.RegisterSession<CustomerSession>();
    });
});

await builder.Build().RunAsync();
