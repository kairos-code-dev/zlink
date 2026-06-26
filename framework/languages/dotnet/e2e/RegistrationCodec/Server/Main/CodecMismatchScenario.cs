using System.Diagnostics;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.DependencyInjection;
using RegistrationCodec.Server.Configuration;
using RegistrationCodec.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

namespace RegistrationCodec.Server.Endpoints;

internal static class CodecMismatchScenario
{
    public static async Task RunAsync(ServerOptions options, CancellationToken cancellationToken)
    {
        var serverProject = Require(options.JsonOnlyPeerProject, "--json-only-peer-project");
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
        process.StartInfo.ArgumentList.Add(serverProject);
        process.StartInfo.ArgumentList.Add("--");
        process.StartInfo.ArgumentList.Add("--rid");
        process.StartInfo.ArgumentList.Add("codec-mismatch");
        process.StartInfo.ArgumentList.Add("--http-url");
        process.StartInfo.ArgumentList.Add(serverUrl);
        process.StartInfo.ArgumentList.Add("--channel-endpoint");
        process.StartInfo.ArgumentList.Add(channelEndpoint);
        process.StartInfo.ArgumentList.Add("--evidence-file");
        process.StartInfo.ArgumentList.Add(evidenceFile);
        process.StartInfo.ArgumentList.Add("--log-dir");
        process.StartInfo.ArgumentList.Add(options.LogDir);
        process.Start();
        var copyOut = CopyToFileAsync(process.StandardOutput, stdout);
        var copyErr = CopyToFileAsync(process.StandardError, stderr);
        try
        {
            await WaitHealthAsync(serverUrl, process, cancellationToken);
            using var mismatchHost = CreateClientHost(channelEndpoint, options.LogDir, "codec-mismatch-client");
            await mismatchHost.StartAsync(cancellationToken);
            var mismatchClient = mismatchHost.Services.GetRequiredService<IZLinkChannelClient>();

            var failed = false;
            try
            {
                await mismatchClient.RequestToChannel(
                        RegistrationCodecNames.Channel,
                        new StringValue { Value = "rc-b5" })
                    .PacketName("EchoProtobuf")
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StringValue>(cancellationToken);
            }
            catch (Exception)
            {
                failed = true;
            }

            if (!failed)
            {
                throw new InvalidOperationException("RC-B5 Protobuf request unexpectedly succeeded against a JSON-only peer.");
            }

            var json = await mismatchClient.RequestToChannel(
                    RegistrationCodecNames.Channel,
                    new JsonEchoReq("rc-b5-json"))
                .PacketName("EchoJson")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>(cancellationToken);
            if (json.Value != "echo:rc-b5-json")
            {
                throw new InvalidOperationException("RC-B5 JSON fallback request did not recover after mismatch.");
            }

            await mismatchHost.StopAsync(cancellationToken);
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }

            await process.WaitForExitAsync(cancellationToken);
            await Task.WhenAll(copyOut, copyErr);
        }
    }

    private static IHost CreateClientHost(string channelEndpoint, string logDir, string nodeId)
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

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }

    private static int PickPort()
    {
        using var socket = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
        socket.Start();
        var port = ((System.Net.IPEndPoint)socket.LocalEndpoint).Port;
        socket.Stop();
        return port;
    }

    private static async Task WaitHealthAsync(string serverUrl, Process process, CancellationToken cancellationToken)
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
                using var response = await http.GetAsync($"{serverUrl}/health", cancellationToken);
                if (response.IsSuccessStatusCode)
                {
                    return;
                }
            }
            catch (HttpRequestException)
            {
            }

            await Task.Delay(250, cancellationToken);
        }

        throw new TimeoutException($"Timed out waiting for RC-B5 mismatch server at {serverUrl}.");
    }

    private static async Task CopyToFileAsync(StreamReader reader, string path)
    {
        await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        await using var writer = new StreamWriter(stream);
        while (await reader.ReadLineAsync() is { } line)
        {
            await writer.WriteLineAsync(line);
            await writer.FlushAsync();
        }
    }
}
