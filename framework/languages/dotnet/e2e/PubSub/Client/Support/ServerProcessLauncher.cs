using System.Diagnostics;

namespace PubSub.Client.Support;

internal sealed class ServerProcessLauncher(ClientOptions options)
{
    public Process StartSubscriber(string name, string httpUrl, string evidenceFile)
    {
        var startInfo = CreateServerStartInfo(options.SubscriberProject, name);
        startInfo.ArgumentList.Add("--http-url");
        startInfo.ArgumentList.Add(httpUrl);
        startInfo.ArgumentList.Add("--registry-router-endpoint");
        startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
        startInfo.ArgumentList.Add("--publisher-endpoint");
        startInfo.ArgumentList.Add(options.PublisherEndpoint);
        startInfo.ArgumentList.Add("--evidence-file");
        startInfo.ArgumentList.Add(Path.Combine(options.LogDir, evidenceFile));
        startInfo.ArgumentList.Add("--log-dir");
        startInfo.ArgumentList.Add(options.LogDir);

        return Start(name, startInfo);
    }

    public Process StartPublisher()
    {
        var startInfo = CreateServerStartInfo(options.PublisherProject, "pub-a");
        startInfo.ArgumentList.Add("--http-url");
        startInfo.ArgumentList.Add(options.PublisherUrl);
        startInfo.ArgumentList.Add("--registry-router-endpoint");
        startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
        startInfo.ArgumentList.Add("--publisher-endpoint");
        startInfo.ArgumentList.Add(options.PublisherEndpoint);
        startInfo.ArgumentList.Add("--evidence-file");
        startInfo.ArgumentList.Add(Path.Combine(options.LogDir, "pub-restart.evidence.log"));
        startInfo.ArgumentList.Add("--log-dir");
        startInfo.ArgumentList.Add(options.LogDir);

        return Start("pub-restart", startInfo);
    }

    private static ProcessStartInfo CreateServerStartInfo(string project, string rid)
    {
        var startInfo = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        startInfo.Environment["ZLINK_E2E_RID"] = rid;
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(project);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--rid");
        startInfo.ArgumentList.Add(rid);
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
