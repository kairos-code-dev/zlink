using Bingo.Server.Configuration;
using Bingo.Server.Api.Handlers;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, SampleApiNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableServer(node.ChannelEndpoint);
                channel.AddHandlerGroup("api");

            }
            {
                var channel = options.AddRouteMeshChannel(SampleNames.PlayChannel);
                channel.EnableServer(node.PlayRouteEndpoint);
                channel.EnableClient(topology.PlayA.PlayChannelEndpoint);
                channel.EnableClient(topology.PlayB.PlayChannelEndpoint);
                channel.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(1);
                channel.ConfigureRouting().RoutingId = node.RouteRid;

            }
        });

        return builder.Build();
    }
}
