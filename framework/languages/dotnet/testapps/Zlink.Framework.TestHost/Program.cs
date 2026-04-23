using System.Text.Json;
using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

var readyFilePath = TryGetReadyFilePath(args);

Console.SetOut(TextWriter.Synchronized(new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false)) { AutoFlush = true }));
var builder = Host.CreateApplicationBuilder(args);

builder.Logging.ClearProviders();
builder.Services.AddHostedService(_ => new ReadySignalHostedService(
    _.GetRequiredService<IHostApplicationLifetime>(),
    readyFilePath));

using var host = builder.Build();
await host.RunAsync();

static string? TryGetReadyFilePath(string[] arguments)
{
    for (var index = 0; index < arguments.Length - 1; index++)
    {
        if (string.Equals(arguments[index], "--ready-file", StringComparison.Ordinal))
        {
            return arguments[index + 1];
        }
    }

    return Environment.GetEnvironmentVariable("ZLINK_TEST_READY_FILE");
}

internal sealed class ReadySignalHostedService(
    IHostApplicationLifetime applicationLifetime,
    string? readyFilePath) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        applicationLifetime.ApplicationStarted.Register(WriteReadyMarker);
        _ = ListenForStopSignalAsync(applicationLifetime, cancellationToken);
        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return Task.CompletedTask;
    }

    private void WriteReadyMarker()
    {
        var payload = JsonSerializer.Serialize(new
        {
            app = "Zlink.Framework.TestHost",
            pid = Environment.ProcessId,
        });

        Console.WriteLine($"READY:{payload}");

        if (!string.IsNullOrWhiteSpace(readyFilePath))
        {
            File.WriteAllText(readyFilePath, payload);
        }
    }

    private async Task ListenForStopSignalAsync(
        IHostApplicationLifetime lifetime,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var line = await Console.In.ReadLineAsync(cancellationToken);

            if (string.Equals(line, "STOP", StringComparison.Ordinal))
            {
                lifetime.StopApplication();
                return;
            }
        }
    }
}
