using Systems.Zlink;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Application.GameCreation;
using TicTacToe.Server.Play.Infrastructure.ZLink;
using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;
using TicTacToe.Server.Play.Infrastructure.ZLink.Handlers;
using TicTacToe.Server.Play.Infrastructure.ZLink.Sessions;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.TicTacToeGameSpot;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Locations.Redis;
using Zlink.Samples.Logging;

namespace TicTacToe.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings.LogDirectory, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddSingleton<ITicTacToeGameRoomProvisioner, TicTacToeGameRoomProvisioner>();
        builder.Services.AddSingleton<TicTacToeGameCreator>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DisableImplicitHandlerAutoRegistration();
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(settings.RedisEndpoint)
                .SetKeyPrefix(settings.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(settings.LogDirectory, settings.InstanceName))
                .TraceLabel(settings.InstanceName);
            options.AddClientServerChannel(SampleChannels.Api)
                .EnableClient(settings.ApiChannelEndpoints[0])
                .EnableClient(settings.ApiChannelEndpoints[1]);

            options.AddClientServerChannel(SampleChannels.Play(settings.PlayIndex))
                .EnableServer(settings.PlayChannelEndpoint)
                .AddRequestHandler<CreateGameHandler, CreateGameReq, CreateGameRes>();

            options.AddStreamNode(SampleNodes.ClientStream)
                .Bind(settings.PlayEndpoint)
                .RegisterSession<PlaySession>();

            options.AddSpotMesh(SampleNodes.PlaySpot)
                .EnableRouter(settings.SpotEndpoint)
                .SetRoutingId(RoutingId.From(settings.PlaySpotNodeRid))
                .ConnectRouter(
                    RoutingId.From(settings.PeerPlaySpotNodeRid), settings.PeerSpotEndpoint)
                .EnablePubSub(settings.SpotPubSubEndpoint)
                .ConnectPeerPub(settings.PeerSpotPubEndpoint)
                .AddEntrySpot<PlayEntrySpot>()
                .AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor)
                .AddActorTransferAdapter<PlayActor, PlayActorTransferAdapter>(SampleTypes.PlayerActor)
                .AddSpotFactory<TicTacToeGame>();
        });

        return builder.Build();
    }
}
