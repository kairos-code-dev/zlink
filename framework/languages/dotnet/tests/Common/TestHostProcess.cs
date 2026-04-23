using System.Diagnostics;
using System.Text.Json;

namespace Zlink.Framework.Tests.Common;

internal sealed class TestHostProcess : IAsyncDisposable
{
    private readonly List<string> _stdout = new();
    private readonly List<string> _stderr = new();
    private readonly Process _process;
    private readonly string _readyFilePath;
    private readonly string _stopFilePath;
    private Task _stdoutPump = Task.CompletedTask;
    private Task _stderrPump = Task.CompletedTask;

    private TestHostProcess(Process process, string readyFilePath, string stopFilePath)
    {
        _process = process;
        _readyFilePath = readyFilePath;
        _stopFilePath = stopFilePath;
    }

    public int ProcessId => _process.Id;

    public IReadOnlyList<string> StandardOutput => _stdout;

    public IReadOnlyList<string> StandardError => _stderr;

    public JsonDocument? ReadyPayload { get; private set; }

    public static async Task<TestHostProcess> StartAsync(
        CancellationToken cancellationToken,
        params string[] additionalArguments)
    {
        var readyFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-testhost-{Guid.NewGuid():N}.json");
        var stopFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-testhost-stop-{Guid.NewGuid():N}.signal");
        var startInfo = FrameworkTestEnvironment.CreateTestHostStartInfo(
            readyFilePath,
            ["--stop-file", stopFilePath, .. additionalArguments],
            redirectStandardInput: false);

        var process = new Process
        {
            StartInfo = startInfo,
            EnableRaisingEvents = true,
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("Failed to start test host process.");
        }

        var host = new TestHostProcess(process, readyFilePath, stopFilePath);
        host._stdoutPump = PumpReaderAsync(process.StandardOutput, host._stdout);
        host._stderrPump = PumpReaderAsync(process.StandardError, host._stderr);
        try
        {
            await host.WaitForReadyAsync(cancellationToken);
            return host;
        }
        catch
        {
            await host.ForceCleanupAsync();
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (_process.HasExited)
        {
            ReadyPayload?.Dispose();
            File.Delete(_readyFilePath);
            _process.Dispose();
            return;
        }

        File.WriteAllText(_stopFilePath, "STOP");

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

        await _stdoutPump;
        await _stderrPump;
        ReadyPayload?.Dispose();
        File.Delete(_readyFilePath);
        File.Delete(_stopFilePath);
        _process.Dispose();
    }

    private async Task WaitForReadyAsync(CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));

        try
        {
            while (!timeout.IsCancellationRequested)
            {
                if (File.Exists(_readyFilePath))
                {
                    var payload = await File.ReadAllTextAsync(_readyFilePath, timeout.Token);
                    using var document = JsonDocument.Parse(payload);

                    if (document.RootElement.TryGetProperty("pid", out var pidElement) &&
                        pidElement.GetInt32() != _process.Id)
                    {
                        throw new InvalidOperationException("READY marker PID does not match the launched process.");
                    }

                    _stdout.Add($"READY:{payload}");
                    ReadyPayload = JsonDocument.Parse(payload);
                    return;
                }

                if (_process.HasExited)
                {
                    throw new InvalidOperationException(
                        $"Test host exited before readiness. Stdout: {string.Join(Environment.NewLine, _stdout)}{Environment.NewLine}Stderr: {string.Join(Environment.NewLine, _stderr)}");
                }

                await Task.Delay(50, timeout.Token);
            }
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Timed out waiting for READY marker. Stdout: {string.Join(Environment.NewLine, _stdout)}{Environment.NewLine}Stderr: {string.Join(Environment.NewLine, _stderr)}");
        }
    }

    public async Task CloseStandardInputAsync()
    {
        _process.StandardInput.Close();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));

        try
        {
            await _process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException)
        {
            throw new TimeoutException(
                $"Timed out waiting for test host exit after stdin EOF. Stdout: {string.Join(Environment.NewLine, _stdout)}{Environment.NewLine}Stderr: {string.Join(Environment.NewLine, _stderr)}");
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

    private async Task ForceCleanupAsync()
    {
        try
        {
            if (!_process.HasExited)
            {
                _process.Kill(entireProcessTree: true);
                await _process.WaitForExitAsync();
            }
        }
        catch
        {
        }

        await _stdoutPump;
        await _stderrPump;
        ReadyPayload?.Dispose();

        if (File.Exists(_readyFilePath))
        {
            File.Delete(_readyFilePath);
        }

        if (File.Exists(_stopFilePath))
        {
            File.Delete(_stopFilePath);
        }

        _process.Dispose();
    }
}
