using System.Diagnostics;
using System.Text.Json;

namespace Zlink.Framework.Tests.Common;

internal sealed class TestHostProcess : IAsyncDisposable
{
    private readonly List<string> _stdout = new();
    private readonly List<string> _stderr = new();
    private readonly Process _process;
    private Task _stderrPump = Task.CompletedTask;

    private TestHostProcess(Process process)
    {
        _process = process;
    }

    public int ProcessId => _process.Id;

    public IReadOnlyList<string> StandardOutput => _stdout;

    public IReadOnlyList<string> StandardError => _stderr;

    public static async Task<TestHostProcess> StartAsync(CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = FrameworkTestEnvironment.GetTestHostExecutablePath(),
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };

        var process = new Process
        {
            StartInfo = startInfo,
            EnableRaisingEvents = true,
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("Failed to start test host process.");
        }

        var host = new TestHostProcess(process);
        host._stderrPump = PumpReaderAsync(process.StandardError, host._stderr);

        await host.WaitForReadyAsync(cancellationToken);
        return host;
    }

    public async ValueTask DisposeAsync()
    {
        if (_process.HasExited)
        {
            _process.Dispose();
            return;
        }

        await _process.StandardInput.WriteLineAsync("STOP");
        await _process.StandardInput.FlushAsync();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));

        try
        {
            await _process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException)
        {
            _process.Kill(entireProcessTree: true);
            await _process.WaitForExitAsync();
        }

        await _stderrPump;
        _process.Dispose();
    }

    private async Task WaitForReadyAsync(CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));

        while (true)
        {
            string? line;
            try
            {
                line = await _process.StandardOutput.ReadLineAsync().WaitAsync(timeout.Token);
            }
            catch (OperationCanceledException)
            {
                throw new TimeoutException(
                    $"Timed out waiting for READY marker. Stdout: {string.Join(Environment.NewLine, _stdout)}{Environment.NewLine}Stderr: {string.Join(Environment.NewLine, _stderr)}");
            }

            if (line is null)
            {
                throw new InvalidOperationException(
                    $"Test host exited before readiness. Stdout: {string.Join(Environment.NewLine, _stdout)}{Environment.NewLine}Stderr: {string.Join(Environment.NewLine, _stderr)}");
            }

            _stdout.Add(line);

            if (!line.StartsWith("READY:", StringComparison.Ordinal))
            {
                continue;
            }

            var payload = line["READY:".Length..];
            using var document = JsonDocument.Parse(payload);

            if (document.RootElement.TryGetProperty("pid", out var pidElement) &&
                pidElement.GetInt32() != _process.Id)
            {
                throw new InvalidOperationException("READY marker PID does not match the launched process.");
            }

            return;
        }
    }

    private static async Task PumpReaderAsync(
        StreamReader reader,
        List<string> sink)
    {
        while (true)
        {
            var line = await reader.ReadLineAsync();

            if (line is null)
            {
                return;
            }

            lock (sink)
            {
                sink.Add(line);
            }
        }
    }
}
