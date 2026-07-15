using System.Diagnostics;
using Zlink.Framework.E2E.Configuration;

namespace PubSub.Client.Support;

internal sealed class ServerProcessLauncher(ClientOptions options)
{
    public Process StartSubscriber(string name, string httpUrl, string evidenceFile)
    {
        var startInfo = CreateServerStartInfo(options.SubscriberProject, name,
        [
            "--rid", name,
            "--http-url", httpUrl,
            "--redis-endpoint", options.RedisEndpoint,
            "--redis-key-prefix", options.RedisKeyPrefix,
            "--evidence-file", Path.Combine(options.LogDir, evidenceFile),
            "--log-dir", options.LogDir,
            "--handler-delay-ms", "0"
        ]);

        return Start(name, startInfo);
    }

    public Process StartPublisher()
    {
        var startInfo = CreateServerStartInfo(options.PublisherProject, "pub-a",
        [
            "--rid", "pub-a",
            "--http-url", options.PublisherUrl,
            "--redis-endpoint", options.RedisEndpoint,
            "--redis-key-prefix", options.RedisKeyPrefix,
            "--publisher-endpoint", options.PublisherEndpoint,
            "--evidence-file", Path.Combine(options.LogDir, "pub-restart.evidence.log"),
            "--log-dir", options.LogDir
        ]);

        return Start("pub-restart", startInfo);
    }

    private ProcessStartInfo CreateServerStartInfo(
        string project,
        string name,
        IReadOnlyList<string> arguments)
    {
        var startInfo = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(project);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--config");
        startInfo.ArgumentList.Add(E2eConfiguration.WriteArguments(options.ConfigDir, name, arguments));
        return startInfo;
    }

    private Process Start(string name, ProcessStartInfo startInfo)
    {
        var stdout = Path.Combine(options.LogDir, $"{name}.stdout.log");
        var stderr = Path.Combine(options.LogDir, $"{name}.stderr.log");
        var process = Process.Start(startInfo)
                      ?? throw new InvalidOperationException($"Failed to start {name}.");
        _ = Task.Run(async () => await File.WriteAllTextAsync(stdout, await process.StandardOutput.ReadToEndAsync()));
        _ = Task.Run(async () => await File.WriteAllTextAsync(stderr, await process.StandardError.ReadToEndAsync()));
        return process;
    }
}
