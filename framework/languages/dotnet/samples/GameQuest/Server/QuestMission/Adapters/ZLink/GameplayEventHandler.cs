using GameQuest.QuestMission.Application;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace GameQuest.QuestMission.Adapters.ZLink;

[ZLinkHandlerGroup("gamequest-gameplay")]
internal sealed class GameplayEventHandler(QuestEventProcessor processor)
{
    [ZLinkPublish]
    public async ValueTask HandleAsync(
        GameplayEventEnvelope gameplayEvent,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        if (context.Topic != SampleNames.GameplayTopic)
        {
            return;
        }

        await processor.ProcessAsync(gameplayEvent, cancellationToken);
    }
}
