using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
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
