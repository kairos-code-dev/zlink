using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using GameQuest.Shared;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Adapters.ZLink.Spots;

internal sealed class PlayerQuestSpot(
    IZLinkSpotContext context,
    ILogger<PlayerQuestSpot> logger) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public string PlayerId { get; private set; } = string.Empty;

    public void Configure()
    {
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        PlayerId = request.FromJson<PlayerQuestSpotCreateReq>().PlayerId;
        logger.LogInformation(
            "gamequest player quest spot ready player={PlayerId} spot={SpotRid}",
            PlayerId,
            Context.SpotRid.ToHex());
        Console.Error.WriteLine($"gamequest player quest spot ready player={PlayerId} spot={Context.SpotRid.ToHex()}");
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed record PlayerQuestSpotCreateReq(string PlayerId);
