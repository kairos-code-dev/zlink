using TicTacToe.SessionActorDispatch.Configuration;

namespace TicTacToe.SessionGateway.Client;

public sealed record SessionActorDispatchClientOptions(
    string PrimaryStreamEndpoint,
    string ReconnectStreamEndpoint,
    string XActorId = SampleNames.XActorId,
    string OActorId = SampleNames.OActorId)
{
    public static SessionActorDispatchClientOptions FromArgs(string[] args)
    {
        string? primaryStreamEndpoint = null;
        string? reconnectStreamEndpoint = null;
        var xActorId = SampleNames.XActorId;
        var oActorId = SampleNames.OActorId;

        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--stream-endpoint":
                    primaryStreamEndpoint = ReadValue(args, ref index);
                    break;
                case "--reconnect-stream-endpoint":
                    reconnectStreamEndpoint = ReadValue(args, ref index);
                    break;
                case "--x-actor":
                    xActorId = ReadValue(args, ref index);
                    break;
                case "--o-actor":
                    oActorId = ReadValue(args, ref index);
                    break;
            }
        }

        if (string.IsNullOrWhiteSpace(primaryStreamEndpoint)
            || string.IsNullOrWhiteSpace(reconnectStreamEndpoint))
        {
            throw new ArgumentException(
                "Usage: dotnet run --project Client/TicTacToe.SessionGateway.Client.csproj -- --stream-endpoint tcp://HOST:PORT --reconnect-stream-endpoint tcp://HOST:PORT");
        }

        return new SessionActorDispatchClientOptions(
            primaryStreamEndpoint,
            reconnectStreamEndpoint,
            xActorId,
            oActorId);
    }

    private static string ReadValue(string[] args, ref int index)
    {
        if (index + 1 >= args.Length)
        {
            throw new ArgumentException($"Missing value for '{args[index]}'.");
        }

        index++;
        return args[index];
    }
}
