using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Session;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
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
    options.ConfigureDispatch().SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>();
    options.AddHandlersFromAssemblyOf(typeof(CustomerSession));
    options.Codecs.AddJson();
    options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
    {
        var channel = options.AddClientServerChannel(SampleNames.TrackingRouteChannel);
        channel.EnableClient();

    }
    {
        var channel = options.AddFanoutChannel(SampleNames.StatusFanoutChannel);
                channel.EnableSubscriber(topology.StatusFanoutEndpoint);
        channel.AddPublishHandler<DeliveryStatusFanoutHandler, DeliveryStatusNotify>();

    }
    {
        var mesh = options.AddSpotMesh(SampleNames.DeliverySpotDiscovery);
        mesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
        {
            var spot = mesh.AddNode(SampleNames.SessionSpotNode);
            {
                var router = spot.EnableRouter(topology.SessionSpotRouterEndpoint);
                router.SetRouterRoutingId(topology.SessionSpotNodeRid);

            }
            {
                var pubsub = spot.EnablePubSub(topology.SessionSpotEndpoint);
                pubsub.SetPubSubRoutingId(topology.SessionSpotPubRid);

            }

        }

    }
    {
        var stream = options.AddStreamNode(SampleNames.CustomerStreamNode);
        stream.AttachActorGateway(SampleNames.SessionSpotNode);
        stream.Bind(topology.SessionStreamEndpoint);
        stream.RegisterSession<CustomerSession>();

    }
});

await builder.Build().RunAsync();
