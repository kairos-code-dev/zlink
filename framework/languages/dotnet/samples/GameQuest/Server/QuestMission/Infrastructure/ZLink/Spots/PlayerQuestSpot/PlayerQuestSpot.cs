using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;

internal sealed class PlayerQuestSpot(
    IZLinkSpotContext context,
    ILogger<PlayerQuestSpot> logger) : IZLinkSpot
{
    public string PlayerId { get; private set; } = string.Empty;
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        PlayerId = request.Decode<PlayerQuestSpotCreateReq>().PlayerId;
        logger.LogInformation(
            "gamequest player quest spot ready player={PlayerId} spot={SpotRid}",
            PlayerId,
            Context.SpotRid.ToString());
        Console.Error.WriteLine($"gamequest player quest spot ready player={PlayerId} spot={Context.SpotRid}");
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed record PlayerQuestSpotCreateReq(string PlayerId);