using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;

sealed record SampleSettings(
    string ApiBindUrl,
    string ApiPublicUrl,
    string ApiChannelEndpoint,
    string PlayChannelEndpoint,
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
                case "--control-endpoint":
                    playChannel = ReadValue();
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
            play ?? "tcp://127.0.0.1:18083",
            spot ?? "tcp://127.0.0.1:18084",
            logDirectory ?? Path.Combine("logs", "playhouse-room-echo"));
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
            PlayEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            SpotEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
        };
    }
}
