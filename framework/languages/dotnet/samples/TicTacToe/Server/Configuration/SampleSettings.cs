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
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace TicTacToe.Server.Configuration;

sealed record SampleSettings(
    string ApiBindUrl,
    string ApiPublicUrl,
    string ApiChannelEndpoint,
    string PlayChannelEndpoint,
    string PlayRouterEndpoint,
    string PlayEndpoint,
    string SpotEndpoint,
    string LogDirectory)
{
    public static SampleSettings FromArgs(string[] args)
    {
        string? apiBind = null;
        string? apiUrl = null;
        string? apiChannel = null;
        string? playChannel = null;
        string? playRouter = null;
        string? play = null;
        string? spot = null;
        string? logDirectory = null;

        for (var i = 0; i < args.Length; i++)
        {
            string? ReadValue()
            {
                if (i + 1 >= args.Length)
                {
                    throw new ArgumentException($"Missing value for '{args[i]}'.");
                }

                i++;
                return args[i];
            }

            switch (args[i])
            {
                case "--api-bind":
                    apiBind = ReadValue();
                    break;
                case "--api-url":
                    apiUrl = ReadValue();
                    break;
                case "--api-channel-endpoint":
                    apiChannel = ReadValue();
                    break;
                case "--play-channel-endpoint":
                    playChannel = ReadValue();
                    break;
                case "--play-router-endpoint":
                    playRouter = ReadValue();
                    break;
                case "--play-endpoint":
                    play = ReadValue();
                    break;
                case "--spot-endpoint":
                    spot = ReadValue();
                    break;
                case "--log-dir":
                    logDirectory = ReadValue();
                    break;
            }
        }

        return new SampleSettings(
            apiBind ?? "http://127.0.0.1:18080",
            apiUrl ?? apiBind ?? "http://127.0.0.1:18080",
            apiChannel ?? "tcp://127.0.0.1:18081",
            playChannel ?? "tcp://127.0.0.1:18082",
            playRouter ?? "tcp://127.0.0.1:18085",
            play ?? "tcp://127.0.0.1:18083",
            spot ?? "tcp://127.0.0.1:18084",
            logDirectory ?? Path.Combine("logs", "tictactoe"));
    }

    public SampleSettings WithEphemeralDefaults()
    {
        var apiPort = SamplePorts.Reserve();
        return this with
        {
            ApiBindUrl = $"http://127.0.0.1:{apiPort}",
            ApiPublicUrl = $"http://127.0.0.1:{apiPort}",
            ApiChannelEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            PlayChannelEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            PlayRouterEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            PlayEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            SpotEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
        };
    }
}
