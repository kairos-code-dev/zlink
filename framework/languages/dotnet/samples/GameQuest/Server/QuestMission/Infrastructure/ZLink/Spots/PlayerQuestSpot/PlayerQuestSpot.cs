using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.Store;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;

internal sealed class PlayerQuestSpot(
    IZLinkSpotContext context,
    QuestEventProcessor processor,
    QuestStore store,
    ILogger<PlayerQuestSpot> logger) : IZLinkSpot
{
    public string PlayerId { get; private set; } = string.Empty;
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
    }

    public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        PlayerId = request.Decode<PlayerQuestSpotCreateReq>().PlayerId;
        await store.RecordOwnerRehydratedAsync(PlayerId, cancellationToken);
        logger.LogInformation(
            "gamequest player quest spot ready player={PlayerId} spot={SpotRid}",
            PlayerId,
            Context.SpotRid.ToString());
        return ZLinkSpotCreateResponse.Accept();
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ApplyGameplayEventRes> ApplyGameplayEventAsync(
        ApplyGameplayEventReq request,
        CancellationToken cancellationToken)
    {
        await processor.ProcessAsync(request.Event, cancellationToken);
        return new ApplyGameplayEventRes(true);
    }

    public ValueTask<SyncQuestProgressRes> SyncAsync(
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        return processor.SyncAsync(request, cancellationToken);
    }
}

internal sealed record PlayerQuestSpotCreateReq(string PlayerId);

internal sealed class ApplyGameplayEventHandler :
    IZLinkSpotRequestHandler<PlayerQuestSpot, ApplyGameplayEventReq, ApplyGameplayEventRes>
{
    public ValueTask<ApplyGameplayEventRes> HandleAsync(
        PlayerQuestSpot spot,
        ApplyGameplayEventReq request,
        CancellationToken cancellationToken)
    {
        return spot.ApplyGameplayEventAsync(request, cancellationToken);
    }
}

internal sealed class SyncQuestProgressHandler :
    IZLinkSpotRequestHandler<PlayerQuestSpot, SyncQuestProgressReq, SyncQuestProgressRes>
{
    public ValueTask<SyncQuestProgressRes> HandleAsync(
        PlayerQuestSpot spot,
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        return spot.SyncAsync(request, cancellationToken);
    }
}
