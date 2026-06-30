using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventPublisher(
    IZLinkFanoutClient fanout,
    GameQuestTopology topology) : IGameplayEventPublisher
{
    private readonly string _apiName = Environment.GetEnvironmentVariable("GAMEQUEST_API_NAME") ?? "api";

    public async ValueTask<string> PublishAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        fanout.Publish(SampleNames.FanoutChannel, SampleNames.GameplayTopic, gameplayEvent)
            .Submit(cancellationToken);
        return topology.FanoutPublisherEndpointForApi(_apiName);
    }
}
