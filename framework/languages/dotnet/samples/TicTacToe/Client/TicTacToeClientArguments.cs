
namespace TicTacToe.Client;

internal static class TicTacToeClientArguments
{
    public static TicTacToeClientOptions Parse(string[] args)
    {
        var defaults = TicTacToeClientOptions.CreateDefault();
        var apiUrl = ReadOption(args, "--api-url") ?? defaults.ApiUrl.ToString();
        var gameName = ReadOption(args, "--game-name") ?? defaults.GameName;
        var xActorId = ReadOption(args, "--x-actor-id") ?? defaults.XActorId;
        var oActorId = ReadOption(args, "--o-actor-id") ?? defaults.OActorId;

        return defaults with
        {
            ApiUrl = new Uri(apiUrl),
            GameName = gameName,
            XActorId = xActorId,
            OActorId = oActorId,
        };
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
