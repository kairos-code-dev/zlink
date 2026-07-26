using GameQuest.GameApi.Application;
using GameQuest.GameApi.Domain;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using System.Text.Json;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    IZLinkRouteClient channels) : IGameplayEventOwnerDispatcher
{
    public async ValueTask<string> DispatchAsync(
        GameplayEvent gameplayEvent,
        CancellationToken cancellationToken)
    {
        await channels.SendToChannel(SampleNames.MeshName, SampleNames.QuestOwnerChannel,
                new GameplayMsg(
                    gameplayEvent.EventId,
                    gameplayEvent.PlayerId,
                    gameplayEvent.EventType,
                    JsonSerializer.SerializeToUtf8Bytes(new GameplayPayload(
                        gameplayEvent.Value,
                        gameplayEvent.Count,
                        gameplayEvent.SourceApi)),
                    gameplayEvent.CreatedAtUnixMs))
            .Async(cancellationToken);
        return SampleNames.QuestOwnerChannel;
    }

    private sealed record GameplayPayload(string Value, int Count, string SourceApi);
}
