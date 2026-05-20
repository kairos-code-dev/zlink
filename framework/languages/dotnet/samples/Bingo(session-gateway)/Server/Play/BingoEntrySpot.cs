namespace Bingo.SessionGateway.Play;

internal sealed class BingoEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddActorPacket<MatchBingoActorHandler, PlayerActor>();
    }
}
