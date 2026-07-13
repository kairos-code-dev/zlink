using GameQuest.QuestMission.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

internal sealed class ZLinkGameApiSnapshotClient(IZLinkChannelClient channels) : IGameApiSnapshotClient
{
    public ValueTask<GetGameplaySnapshotRes> ReadSnapshotAsync(
        GetGameplaySnapshotReq request,
        CancellationToken cancellationToken)
    {
        return channels.RequestToChannel(SampleNames.GameApiChannel, request)
            .Async<GetGameplaySnapshotRes>(cancellationToken);
    }
}

internal sealed class ZLinkQuestProgressNotifier(IZLinkChannelClient channels) : IQuestProgressNotifier
{
    public async ValueTask<QuestProgressNotifyResult> NotifyAsync(
        string sourceApi,
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        try
        {
            var response = await channels.RequestToChannel(
                    SampleNames.GameApiChannel,
                    request)
                .Async<NotifyQuestProgressRes>(cancellationToken);
            return new QuestProgressNotifyResult(response.Delivered, null, null);
        }
        catch (ZLinkFrameworkException error)
        {
            return new QuestProgressNotifyResult(false, null, error);
        }
    }
}
