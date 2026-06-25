using System.Diagnostics;
using System.Net.Http.Json;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RegistrationCodec.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var host = Host.CreateDefaultBuilder(args)
    .ConfigureServices(services =>
    {
        services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
                .TraceLabel("client");
            framework.Codecs.AddJson();
            framework.Codecs.Use(ZLinkProtobufCodec.Default);
            framework.Codecs.Use(ZLinkMessagePackCodec.Default);
            framework.AddClientServerChannel(RegistrationCodecNames.Channel)
                .EnableClient(options.ChannelEndpoint);
        });
    })
    .Build();
await host.StartAsync();
var client = host.Services.GetRequiredService<IZLinkChannelClient>();
using var http = new HttpClient();

await RunRcA1Async(client, http, options);
await RunRcA2Async(client, http, options);
await RunRcA3Async(client, http, options);
await RunRcA4A5Async(client, http, options);
await RunRcA6Async(options);
await RunRcB1B2B3B4Async(client, http, options);
await RunRcB5Async(options);

Console.WriteLine("registration-codec e2e result=passed");
await host.StopAsync();

static async Task RunRcA1Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var reply = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoAutoReq("rc-a1"))
        .Async<EchoReply>();
    Ensure(reply.Value == "echo:rc-a1", "RC-A1 request reply mismatch.");

    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoAutoCommand("cmd-rc-a1", "rc-a1-send"))
        .Async();
    await WaitForEvidenceLineAsync(http, options.ServerUrl,
        line => line.Contains("echo-command|variant=auto|id=cmd-rc-a1", StringComparison.Ordinal),
        "RC-A1 send evidence missing.");
    Console.WriteLine("scenario RC-A1 passed");
}

static async Task RunRcA2Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var reply = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a2"))
        .PacketName("EchoAttr")
        .Async<EchoReply>();
    Ensure(reply.Value == "echo:rc-a2", "RC-A2 request reply mismatch.");

    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoCommand("cmd-rc-a2", "rc-a2-send"))
        .PacketName("EchoAttrCommand")
        .Async();
    await WaitForEvidenceLineAsync(http, options.ServerUrl,
        line => line.Contains("echo-command|variant=attr|id=cmd-rc-a2", StringComparison.Ordinal),
        "RC-A2 send evidence missing.");
    Console.WriteLine("scenario RC-A2 passed");
}

static async Task RunRcA3Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var reply = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoManualReq("rc-a3"))
        .Async<EchoReply>();
    Ensure(reply.Value == "echo:rc-a3", "RC-A3 request reply mismatch.");

    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoManualCommand("cmd-rc-a3", "rc-a3-send"))
        .Async();
    await WaitForEvidenceLineAsync(http, options.ServerUrl,
        line => line.Contains("echo-command|variant=manual|id=cmd-rc-a3", StringComparison.Ordinal),
        "RC-A3 send evidence missing.");
    Console.WriteLine("scenario RC-A3 passed");
}

static async Task RunRcA4A5Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var first = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a4-1"))
        .PacketName("EchoDi")
        .Async<EchoReply>();
    var second = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a4-2"))
        .PacketName("EchoDi")
        .Async<EchoReply>();
    Ensure(first.Value == "echo:rc-a4-1" && second.Value == "echo:rc-a4-2", "RC-A4 DI reply mismatch.");

    await WaitForEvidenceSnapshotAsync(http, options.ServerUrl,
        lines =>
        {
            var di = lines.Where(line => line.Contains("di|", StringComparison.Ordinal)).ToArray();
            if (di.Length < 2)
            {
                return false;
            }

            var singletonIds = di.Select(line => ExtractValue(line, "singleton")).Distinct(StringComparer.Ordinal).Count();
            var scopedIds = di.Select(line => ExtractValue(line, "scoped")).Distinct(StringComparer.Ordinal).Count();
            return singletonIds == 1 && scopedIds >= 2;
        },
        "RC-A4 expected stable singleton and per-dispatch scoped dependencies.");

    var evidence = await ReadEvidenceAsync(http, options.ServerUrl);
    var filter = evidence
        .Where(line => line.Contains("filter|", StringComparison.Ordinal)
            && line.Contains("packet=EchoDi", StringComparison.Ordinal))
        .Take(4)
        .ToArray();
    Ensure(filter.Length == 4, "RC-A5 filter evidence missing.");
    Ensure(filter[0].Contains("name=first|phase=before", StringComparison.Ordinal)
        && filter[1].Contains("name=second|phase=before", StringComparison.Ordinal)
        && filter[2].Contains("name=second|phase=after", StringComparison.Ordinal)
        && filter[3].Contains("name=first|phase=after", StringComparison.Ordinal),
        "RC-A5 filter ordering mismatch.");
    Console.WriteLine("scenario RC-A4 passed");
    Console.WriteLine("scenario RC-A5 passed");
}

static async Task RunRcA6Async(ClientOptions options)
{
    var invalidHttpPort = PickPort();
    var invalidChannelPort = PickPort();
    var stdout = Path.Combine(options.LogDir, "invalid-duplicate.stdout.log");
    var stderr = Path.Combine(options.LogDir, "invalid-duplicate.stderr.log");
    using var process = new Process();
    process.StartInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    process.StartInfo.ArgumentList.Add("run");
    process.StartInfo.ArgumentList.Add("--project");
    process.StartInfo.ArgumentList.Add(options.ServerProject);
    process.StartInfo.ArgumentList.Add("--");
    process.StartInfo.ArgumentList.Add("--rid");
    process.StartInfo.ArgumentList.Add("invalid-duplicate");
    process.StartInfo.ArgumentList.Add("--http-url");
    process.StartInfo.ArgumentList.Add($"http://127.0.0.1:{invalidHttpPort}");
    process.StartInfo.ArgumentList.Add("--channel-endpoint");
    process.StartInfo.ArgumentList.Add($"tcp://127.0.0.1:{invalidChannelPort}");
    process.StartInfo.ArgumentList.Add("--invalid-mode");
    process.StartInfo.ArgumentList.Add("duplicate");
    process.StartInfo.ArgumentList.Add("--log-dir");
    process.StartInfo.ArgumentList.Add(options.LogDir);
    process.Start();
    var completed = await Task.Run(() => process.WaitForExit(15_000));
    File.WriteAllText(stdout, await process.StandardOutput.ReadToEndAsync());
    var errorText = await process.StandardError.ReadToEndAsync();
    File.WriteAllText(stderr, errorText);
    Ensure(completed, "RC-A6 invalid registration server did not exit.");
    Ensure(process.ExitCode != 0, "RC-A6 invalid registration server unexpectedly started.");
    Ensure(errorText.Contains("duplicate", StringComparison.OrdinalIgnoreCase)
        || errorText.Contains("EchoManual", StringComparison.Ordinal),
        "RC-A6 invalid registration error did not mention duplicate registration.");
    Console.WriteLine("scenario RC-A6 passed");
}

static async Task RunRcB1B2B3B4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var json = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new JsonEchoReq("rc-b1"))
        .PacketName("EchoJson")
        .Async<EchoReply>();
    Ensure(json.Value == "echo:rc-b1", "RC-B1 JSON reply mismatch.");
    Ensure(json.ContentType == "application/json", "RC-B1 JSON content type mismatch.");
    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new JsonEchoCommand("cmd-rc-b1", "rc-b1-send"))
        .PacketName("EchoJsonCommand")
        .Async();

    var protobuf = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new StringValue { Value = "rc-b2" })
        .PacketName("EchoProtobuf")
        .Async<StringValue>();
    Ensure(protobuf.Value.Contains("echo:rc-b2", StringComparison.Ordinal), "RC-B2 Protobuf reply mismatch.");
    Ensure(protobuf.Value.Contains("content:application/x-protobuf", StringComparison.Ordinal),
        "RC-B2 Protobuf content type mismatch.");
    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new StringValue { Value = "rc-b2-send" })
        .PacketName("EchoProtobufCommand")
        .Async();

    var packed = await client.RequestToChannel(
            RegistrationCodecNames.Channel,
            new PackedEchoReq { Value = "rc-b3" })
        .PacketName("EchoMessagePack")
        .Async<PackedEchoReq>();
    Ensure(packed.Value.Contains("echo:rc-b3", StringComparison.Ordinal), "RC-B3 MessagePack reply mismatch.");
    Ensure(packed.Value.Contains("content:application/x-msgpack", StringComparison.Ordinal),
        "RC-B3 MessagePack content type mismatch.");
    await client.SendToChannel(
            RegistrationCodecNames.Channel,
            new PackedEchoCommand { CommandId = "cmd-rc-b3", Value = "rc-b3-send" })
        .PacketName("EchoMessagePackCommand")
        .Async();

    await WaitForEvidenceSnapshotAsync(http, options.ServerUrl,
        lines => HasCodec(lines, "json", "application/json")
            && HasCodec(lines, "protobuf", "application/x-protobuf")
            && HasCodec(lines, "msgpack", "application/x-msgpack"),
        "RC-B4 expected all codec evidence.");

    Console.WriteLine("scenario RC-B1 passed");
    Console.WriteLine("scenario RC-B2 passed");
    Console.WriteLine("scenario RC-B3 passed");
    Console.WriteLine("scenario RC-B4 passed");
}

static async Task RunRcB5Async(ClientOptions options)
{
    var httpPort = PickPort();
    var channelPort = PickPort();
    var serverUrl = $"http://127.0.0.1:{httpPort}";
    var channelEndpoint = $"tcp://127.0.0.1:{channelPort}";
    var evidenceFile = Path.Combine(options.LogDir, "codec-mismatch.evidence.log");
    var stdout = Path.Combine(options.LogDir, "codec-mismatch.stdout.log");
    var stderr = Path.Combine(options.LogDir, "codec-mismatch.stderr.log");
    using var process = new Process();
    process.StartInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    process.StartInfo.ArgumentList.Add("run");
    process.StartInfo.ArgumentList.Add("--project");
    process.StartInfo.ArgumentList.Add(options.ServerProject);
    process.StartInfo.ArgumentList.Add("--");
    process.StartInfo.ArgumentList.Add("--rid");
    process.StartInfo.ArgumentList.Add("codec-mismatch");
    process.StartInfo.ArgumentList.Add("--http-url");
    process.StartInfo.ArgumentList.Add(serverUrl);
    process.StartInfo.ArgumentList.Add("--channel-endpoint");
    process.StartInfo.ArgumentList.Add(channelEndpoint);
    process.StartInfo.ArgumentList.Add("--codec-mode");
    process.StartInfo.ArgumentList.Add("json-only");
    process.StartInfo.ArgumentList.Add("--evidence-file");
    process.StartInfo.ArgumentList.Add(evidenceFile);
    process.StartInfo.ArgumentList.Add("--log-dir");
    process.StartInfo.ArgumentList.Add(options.LogDir);
    process.Start();
    var copyOut = CopyToFileAsync(process.StandardOutput, stdout);
    var copyErr = CopyToFileAsync(process.StandardError, stderr);
    try
    {
        await WaitHealthAsync(serverUrl, process);
        using var mismatchHost = CreateClientHost(channelEndpoint, options.LogDir, "codec-mismatch-client");
        await mismatchHost.StartAsync();
        var mismatchClient = mismatchHost.Services.GetRequiredService<IZLinkChannelClient>();

        var failed = false;
        try
        {
            await mismatchClient.RequestToChannel(
                    RegistrationCodecNames.Channel,
                    new StringValue { Value = "rc-b5" })
                .PacketName("EchoProtobuf")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StringValue>();
        }
        catch (Exception)
        {
            failed = true;
        }

        Ensure(failed, "RC-B5 Protobuf request unexpectedly succeeded against a JSON-only peer.");

        var json = await mismatchClient.RequestToChannel(
                RegistrationCodecNames.Channel,
                new JsonEchoReq("rc-b5-json"))
            .PacketName("EchoJson")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<EchoReply>();
        Ensure(json.Value == "echo:rc-b5-json", "RC-B5 JSON fallback request did not recover after mismatch.");

        await mismatchHost.StopAsync();
    }
    finally
    {
        if (!process.HasExited)
        {
            process.Kill(entireProcessTree: true);
        }

        await process.WaitForExitAsync();
        await Task.WhenAll(copyOut, copyErr);
    }

    Console.WriteLine("scenario RC-B5 passed");
}

static IHost CreateClientHost(string channelEndpoint, string logDir, string nodeId)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(logDir, $"{nodeId}-flow.log"))
                    .TraceLabel(nodeId);
                framework.Codecs.AddJson();
                framework.Codecs.Use(ZLinkProtobufCodec.Default);
                framework.Codecs.Use(ZLinkMessagePackCodec.Default);
                framework.AddClientServerChannel(RegistrationCodecNames.Channel)
                    .EnableClient(channelEndpoint);
            });
        })
        .Build();
}

static async Task WaitHealthAsync(string serverUrl, Process process)
{
    using var http = new HttpClient();
    for (var i = 0; i < 120; i++)
    {
        if (process.HasExited)
        {
            throw new InvalidOperationException($"RC-B5 mismatch server exited early: {process.ExitCode}.");
        }

        try
        {
            using var response = await http.GetAsync($"{serverUrl}/health");
            if (response.IsSuccessStatusCode)
            {
                return;
            }
        }
        catch
        {
        }

        await Task.Delay(250);
    }

    throw new TimeoutException($"Timed out waiting for RC-B5 mismatch server at {serverUrl}.");
}

static async Task CopyToFileAsync(StreamReader reader, string path)
{
    await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
    await using var writer = new StreamWriter(stream);
    while (await reader.ReadLineAsync() is { } line)
    {
        await writer.WriteLineAsync(line);
        await writer.FlushAsync();
    }
}

static bool HasCodec(string[] lines, string codec, string contentType)
{
    return lines.Any(line => line.Contains($"codec-request|codec={codec}", StringComparison.Ordinal)
        && line.Contains($"content={contentType}", StringComparison.Ordinal))
        && lines.Any(line => line.Contains($"codec-command|codec={codec}", StringComparison.Ordinal)
            && line.Contains($"content={contentType}", StringComparison.Ordinal));
}

static async Task WaitForEvidenceLineAsync(
    HttpClient http,
    string serverUrl,
    Func<string, bool> predicate,
    string failureMessage)
{
    await WaitForEvidenceSnapshotAsync(http, serverUrl, lines => lines.Any(predicate), failureMessage);
}

static async Task WaitForEvidenceSnapshotAsync(
    HttpClient http,
    string serverUrl,
    Func<string[], bool> predicate,
    string failureMessage)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
    while (!timeout.IsCancellationRequested)
    {
        var evidence = await ReadEvidenceAsync(http, serverUrl);
        if (predicate(evidence))
        {
            return;
        }

        await Task.Delay(100, timeout.Token).ContinueWith(_ => { });
    }

    throw new InvalidOperationException(failureMessage);
}

static async Task<string[]> ReadEvidenceAsync(HttpClient http, string serverUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{serverUrl}/evidence") ?? [];
}

static string ExtractValue(string line, string key)
{
    var marker = key + "=";
    var start = line.IndexOf(marker, StringComparison.Ordinal);
    if (start < 0)
    {
        return string.Empty;
    }

    start += marker.Length;
    var end = line.IndexOf('|', start);
    return end < 0 ? line[start..] : line[start..end];
}

static int PickPort()
{
    using var socket = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
    socket.Start();
    var port = ((System.Net.IPEndPoint)socket.LocalEndpoint).Port;
    socket.Stop();
    return port;
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

internal sealed record ClientOptions(
    string ChannelEndpoint,
    string ServerUrl,
    string ServerProject,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string Get(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        return new ClientOptions(
            ChannelEndpoint: Get("--channel-endpoint"),
            ServerUrl: Get("--server-url"),
            ServerProject: Get("--server-project"),
            LogDir: Get("--log-dir"));
    }
}
