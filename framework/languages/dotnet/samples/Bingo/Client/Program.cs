namespace Bingo.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        if (args.Length == 0)
        {
            throw new ArgumentException("Usage: --stream-endpoint tcp://HOST:PORT");
        }

        var streamEndpoint = ReadOption(args, "--stream-endpoint")
            ?? throw new ArgumentException("Missing --stream-endpoint.");
        var result = await new BingoClientApp().RunAsync(
            new BingoClientOptions(streamEndpoint));
        result.WriteTo(Console.Out);
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
