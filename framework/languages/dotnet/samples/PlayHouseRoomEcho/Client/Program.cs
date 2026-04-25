using System.Net;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text;

var apiUrl = ReadOption(args, "--api-url") ?? "http://127.0.0.1:18080";
var roomName = ReadOption(args, "--room-name") ?? "echo-room";
var playerId = ReadOption(args, "--player-id") ?? "player-1";
var message = ReadOption(args, "--message") ?? "o";

using var http = new HttpClient
{
    BaseAddress = new Uri(apiUrl),
    Timeout = TimeSpan.FromSeconds(10),
};

var room = await http.PostAsJsonAsync("/rooms", new CreateRoomRequest(roomName))
    .ContinueWith(static async task =>
    {
        using var response = await task.ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<CreateRoomReply>()
            .ConfigureAwait(false)
            ?? throw new InvalidOperationException("API returned an empty room response.");
    })
    .Unwrap()
    .ConfigureAwait(false);

Console.WriteLine($"api: created room {room.RoomId} at {room.PlayEndpoint}");

using var tcp = ConnectToPlay(room.PlayEndpoint);
var stream = tcp.GetStream();
stream.ReadTimeout = 5000;
stream.WriteTimeout = 5000;

SendLine(stream, $"join-room|{room.RoomId}|{playerId}");
var join = ReceiveLine(stream);
Console.WriteLine($"client: {join}");

SendLine(stream, $"echo|{message}");
var echo = ReceiveLine(stream);
Console.WriteLine($"client: {echo}");

var expected = $"echo-reply|{message}";
if (!string.Equals(echo, expected, StringComparison.Ordinal))
{
    throw new InvalidOperationException($"Unexpected echo reply: {echo}");
}

static TcpClient ConnectToPlay(string zlinkEndpoint)
{
    var uri = new Uri(zlinkEndpoint);
    var client = new TcpClient();
    client.Connect(IPAddress.Parse(uri.Host), uri.Port);
    return client;
}

static void SendLine(NetworkStream stream, string line)
{
    var payload = Encoding.UTF8.GetBytes(line + "\n");
    stream.Write(payload);
    stream.Flush();
}

static string ReceiveLine(NetworkStream stream)
{
    var bytes = new List<byte>();
    while (true)
    {
        var value = stream.ReadByte();
        if (value == '\n')
        {
            return Encoding.UTF8.GetString(bytes.ToArray());
        }

        if (value == '\r')
        {
            continue;
        }

        if (value < 0)
        {
            throw new IOException("The play server closed the stream.");
        }

        bytes.Add((byte)value);
        if (bytes.Count > 64 * 1024)
        {
            throw new InvalidDataException("Line frame is too large.");
        }
    }
}

static string? ReadOption(string[] args, string name)
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

sealed record CreateRoomRequest(string? RoomName);

sealed record CreateRoomReply(string RoomId, string PlayEndpoint, string RoomName);
