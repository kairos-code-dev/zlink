using Microsoft.Extensions.Configuration;

namespace TicTacToe.Server.Configuration;

sealed record SampleSettings(
    string ApiBindUrl,
    string ApiPublicUrl,
    string ApiChannelEndpoint,
    string PlayChannelEndpoint,
    string PlayEndpoint,
    string SpotEndpoint,
    string LogDirectory)
{
    public static SampleSettings Load(string[] args)
    {
        var defaults = CreateDefault();
        var configPath = ReadOption(args, "--config");
        var builder = new ConfigurationBuilder();
        if (!string.IsNullOrWhiteSpace(configPath))
        {
            builder.AddJsonFile(configPath, optional: false);
        }

        builder.AddEnvironmentVariables("TICTACTOE_");
        var section = builder.Build().GetSection("Sample");
        var configured = new SampleSettings(
            section[nameof(ApiBindUrl)] ?? defaults.ApiBindUrl,
            section[nameof(ApiPublicUrl)] ?? defaults.ApiPublicUrl,
            section[nameof(ApiChannelEndpoint)] ?? defaults.ApiChannelEndpoint,
            section[nameof(PlayChannelEndpoint)] ?? defaults.PlayChannelEndpoint,
            section[nameof(PlayEndpoint)] ?? defaults.PlayEndpoint,
            section[nameof(SpotEndpoint)] ?? defaults.SpotEndpoint,
            section[nameof(LogDirectory)] ?? defaults.LogDirectory);

        return ApplyArgs(configured, args);
    }

    private static SampleSettings CreateDefault()
        => new(
            "http://127.0.0.1:18080",
            "http://127.0.0.1:18080",
            "tcp://127.0.0.1:18081",
            "tcp://127.0.0.1:18082",
            "tcp://127.0.0.1:18083",
            "tcp://127.0.0.1:18084",
            Path.Combine("logs", "tictactoe"));

    private static SampleSettings ApplyArgs(SampleSettings defaults, string[] args)
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
            apiBind ?? defaults.ApiBindUrl,
            apiUrl ?? apiBind ?? defaults.ApiPublicUrl,
            apiChannel ?? defaults.ApiChannelEndpoint,
            playChannel ?? defaults.PlayChannelEndpoint,
            play ?? defaults.PlayEndpoint,
            spot ?? defaults.SpotEndpoint,
            logDirectory ?? defaults.LogDirectory);
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        if (index < 0)
        {
            return null;
        }

        if (index + 1 >= args.Length)
        {
            throw new ArgumentException($"Missing value for '{name}'.");
        }

        return args[index + 1];
    }
}
