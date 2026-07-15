using GameQuest.GameApi.Application;
using GameQuest.GameApi.Domain;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using System.Text.Json;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    GameQuestTopology topology,
    IZLinkChannelClient channels) : IGameplayEventOwnerDispatcher
{
    public ValueTask<string> DispatchAsync(
        GameplayEvent gameplayEvent,
        CancellationToken cancellationToken)
    {
        var owner = topology.OwnerRouteRid(gameplayEvent.PlayerId);
        channels.SendToChannel(
                topology.QuestOwnerChannel(gameplayEvent.PlayerId),
                new GameplayMsg(
                    gameplayEvent.EventId,
                    gameplayEvent.PlayerId,
                    gameplayEvent.EventType,
                    JsonSerializer.SerializeToUtf8Bytes(new GameplayPayload(
                        gameplayEvent.Value,
                        gameplayEvent.Count,
                        gameplayEvent.SourceApi)),
                    gameplayEvent.CreatedAtUnixMs))
            .Submit(cancellationToken);
        return ValueTask.FromResult(owner.ToString());
    }

    private sealed record GameplayPayload(string Value, int Count, string SourceApi);
}
