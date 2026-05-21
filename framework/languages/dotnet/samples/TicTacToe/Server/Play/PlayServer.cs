using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using TicTacToe.Server.Play.Actors;
using TicTacToe.Server.Play.Sessions;

namespace TicTacToe.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings, "play");

        builder.Services.AddSingleton(settings);

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<PlayServer>();
            options.AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor);

            options.AddClientServerChannel(SampleChannels.Api, channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections =>
                    {
                        connections.Connect(settings.ApiChannelEndpoint);
                    });
                });
            });

            options.AddClientServerChannel(SampleChannels.Play, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.PlayChannelEndpoint);
                });
                channel.AddHandlerGroup("play");
            });

            options.AddActorSessionBindingStore<InMemoryActorSessionBindingStore>();
            options.AddRouteMeshChannel(SampleChannels.Router, routed =>
            {
                routed.Bind(settings.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.FromString(SampleTypes.PlayRouterId));
                routed.UseManualConnections(connections => connections.Connect(settings.PlayRouterEndpoint));
            });

            options.AddStreamNode(SampleNodes.ClientStream, stream =>
            {
                stream.Bind(settings.PlayEndpoint);
                stream.AddHeaderSession<PlaySession>();
            });

            options.AddSpotNode(SampleNodes.PlaySpot, spot =>
            {
                spot.Bind(settings.SpotEndpoint);
                spot.AddEntrySpot<PlayEntrySpot>();
                spot.AddSpotFactory<TicTacToeGame>(SampleTypes.GameSpot);
            });
        });

        return builder.Build();
    }
}
