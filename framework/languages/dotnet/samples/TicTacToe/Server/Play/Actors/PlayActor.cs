using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Server.Api;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Server.Play.EntrySpot;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Server.Play.Sessions;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Zlink.Framework.Runtime.Core;

namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActor(
    string actorId,
    IZLinkActorContext context)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public string GameId { get; private set; } = string.Empty;

    public void JoinGame(string gameId)
    {
        if (string.IsNullOrWhiteSpace(gameId))
        {
            throw new ArgumentException("Game id must not be empty.", nameof(gameId));
        }

        GameId = gameId;
    }

    public string RequireJoinedGame()
    {
        if (!Context.IsJoined || string.IsNullOrEmpty(GameId))
        {
            throw new InvalidOperationException("Actor has not joined a game.");
        }

        return GameId;
    }

}
