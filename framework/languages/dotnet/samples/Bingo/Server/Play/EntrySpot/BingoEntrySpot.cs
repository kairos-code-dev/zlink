using Bingo.Server.Play.EntrySpot.Handlers;

namespace Bingo.Server.Play.EntrySpot;

internal sealed class BingoEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddHandler<MatchBingoActorHandler>();
        Context.AddHandler<BingoEntrySpotActorJoinedHandler>();
        Context.AddHandler<BingoEntrySpotActorLeftHandler>();
    }
}
