using Microsoft.Extensions.Configuration;

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
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(builder.Logging, settings.LogDirectory, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddSingleton<ITicTacToeGameRoomProvisioner, TicTacToeGameRoomProvisioner>();
        builder.Services.AddSingleton<TicTacToeGameCreator>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DisableImplicitHandlerAutoRegistration();
            options.ActorTransferTimeout = TimeSpan.FromSeconds(15);
            options.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(settings.RedisEndpoint)
                .SetKeyPrefix(settings.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(settings.LogDirectory, settings.InstanceName))
                .TraceLabel(settings.InstanceName);
            options.AddStreamNode(SampleNodes.ClientStream)
                .Bind(settings.PlayEndpoint)
                .EnableActorDispatch(SampleNodes.Mesh)
                .AddSession<PlaySession>();

            var mesh = options.AddRouteMesh(SampleNodes.Mesh)
                .Listen(settings.MeshEndpoint)
                .SetRoutingId(RoutingId.From(settings.PlayMeshNodeRid))
                .AddEntrySpot<PlayEntrySpot>()
                .AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor)
                .AddActorTransferAdapter<PlayActor, PlayActorTransferAdapter>(SampleTypes.PlayerActor)
                .AddSpotFactory<TicTacToeGame>();
            mesh.ChannelName(SampleChannels.Api).SetWeight(0);
            var playA = mesh.ChannelName(SampleChannels.Play(0))
                .SetWeight(settings.PlayIndex == 0 ? 100 : 0);
            var playB = mesh.ChannelName(SampleChannels.Play(1))
                .SetWeight(settings.PlayIndex == 1 ? 100 : 0);
            (settings.PlayIndex == 0 ? playA : playB)
                .AddRequestHandler<CreateGameHandler, CreateGameReq, CreateGameRes>();
            mesh.ChannelName(SampleTopics.PlayerMilestoneChannel);
            foreach (var endpoint in settings.PeerMeshEndpoints)
                mesh.PeerConnections.Connect(endpoint);
        });

        return builder.Build();
    }
}
