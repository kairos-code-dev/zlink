using System.Net.Http.Json;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Options;

namespace PlayHouseRoomEcho.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var apiUrl = ReadOption(args, "--api-url") ?? "http://127.0.0.1:18080";
        var roomName = ReadOption(args, "--room-name") ?? "echo-room";
        var playerId = ReadOption(args, "--player-id") ?? "player-1";
        var message = ReadOption(args, "--message") ?? "o";

        using var http = new HttpClient();
        http.BaseAddress = new Uri(apiUrl);
        http.Timeout = TimeSpan.FromSeconds(10);

        var room = await CreateRoomAsync(http, roomName).ConfigureAwait(false);
        Console.WriteLine($"api: created room {room.RoomId} at {room.PlayEndpoint}");

        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(room.PlayEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            SendTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
        }).ConfigureAwait(false);

        var join = await connector
            .Request(new JoinRoom(room.RoomId, playerId))
            .ExecAsync<JoinRoomAccepted>()
            .ConfigureAwait(false);
        Console.WriteLine($"client: {nameof(JoinRoomAccepted)}|{join.RoomId}|{join.PlayerId}");

        var echo = await connector
            .Request(new Echo(message))
            .ExecAsync<EchoReply>()
            .ConfigureAwait(false);
        Console.WriteLine($"client: {nameof(EchoReply)}|{echo.Text}");

        if (!string.Equals(echo.Text, message, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unexpected echo reply: {nameof(EchoReply)}|{echo.Text}");
        }
    }

    private static async Task<CreateRoomReply> CreateRoomAsync(HttpClient http, string roomName)
    {
        using var response = await http.PostAsJsonAsync("/rooms", new CreateRoomRequest(roomName))
            .ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<CreateRoomReply>().ConfigureAwait(false)
               ?? throw new InvalidOperationException("API returned an empty room response.");
    }

    private static string? ReadOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length; i++)
        {
            if (!string.Equals(args[i], name, StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{name}'.");
            }

            return args[i + 1];
        }

        return null;
    }
}

internal sealed record CreateRoomRequest(string? RoomName);

internal sealed record CreateRoomReply(string RoomId, string PlayEndpoint, string RoomName);

internal sealed record JoinRoom(string RoomId, string PlayerId);

internal sealed record JoinRoomAccepted(string RoomId, string PlayerId);

internal sealed record Echo(string Text);

internal sealed record EchoReply(string Text);
