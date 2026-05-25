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

namespace TicTacToe.Server.Configuration;

internal static class SampleChannels
{
    public const string Api = "Api";
    public const string Play = "Play";

    public const string Router = "Gateway";
}

internal static class SampleTypes
{
    public const string PlayerActor = "player";
    public const string GameSpot = "tictactoe-game";

    public const string PlayRouterId = "1001";
}

internal static class SampleDefaults
{
    public const string GameName = "tictactoe-game";
}

internal static class SampleNodes
{
    public const string ClientStream = "client-stream";
    public const string PlaySpot = "play-node";
}

internal static class SampleTimeouts
{
    public static TimeSpan Request { get; } = TimeSpan.FromSeconds(5);
}
