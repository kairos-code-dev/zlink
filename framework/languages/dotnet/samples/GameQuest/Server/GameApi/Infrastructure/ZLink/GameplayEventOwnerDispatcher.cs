using GameQuest.GameApi.Application;
using GameQuest.GameApi.Domain;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using System.Text.Json;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    GameQuestTopology topology,
    IZLinkRouteClient channels) : IGameplayEventOwnerDispatcher
{
    public async ValueTask<string> DispatchAsync(
        GameplayEvent gameplayEvent,
        CancellationToken cancellationToken)
    {
        var owner = topology.OwnerRouteRid(gameplayEvent.PlayerId);
        await channels.SendToChannel(SampleNames.MeshName, topology.QuestOwnerChannel(gameplayEvent.PlayerId),
                new GameplayMsg(
                    gameplayEvent.EventId,
                    gameplayEvent.PlayerId,
                    gameplayEvent.EventType,
                    JsonSerializer.SerializeToUtf8Bytes(new GameplayPayload(
                        gameplayEvent.Value,
                        gameplayEvent.Count,
                        gameplayEvent.SourceApi)),
                    gameplayEvent.CreatedAtUnixMs))
            .SubmitAsync(cancellationToken);
        return owner.ToString();
    }

    private sealed record GameplayPayload(string Value, int Count, string SourceApi);
}
