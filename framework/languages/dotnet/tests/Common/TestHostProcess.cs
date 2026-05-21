using System.Diagnostics;
using System.Text.Json;

namespace Zlink.Framework.E2ETests;

internal sealed class TestHostProcess : IAsyncDisposable
{
    private readonly List<string> _stdout = new();
    private readonly List<string> _stderr = new();
    private readonly Process _process;
    private readonly string _stopFilePath;
    private readonly string _logDirectoryPath;
    private readonly string _stdoutLogPath;
    private readonly string _stderrLogPath;
    private readonly TaskCompletionSource<string> _readyPayloadSource =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private Task _stdoutPump = Task.CompletedTask;
    private Task _stderrPump = Task.CompletedTask;

    private TestHostProcess(Process process, string stopFilePath, string logDirectoryPath)
    {
        _process = process;
        _stopFilePath = stopFilePath;
        _logDirectoryPath = logDirectoryPath;
        _stdoutLogPath = Path.Combine(logDirectoryPath, "stdout.log");
        _stderrLogPath = Path.Combine(logDirectoryPath, "stderr.log");
    }

    public int ProcessId => _process.Id;

    public IReadOnlyList<string> StandardOutput => _stdout;

    public IReadOnlyList<string> StandardError => _stderr;

    public string LogDirectoryPath => _logDirectoryPath;

    public string StandardOutputLogPath => _stdoutLogPath;

    public string StandardErrorLogPath => _stderrLogPath;

    public JsonDocument? ReadyPayload { get; private set; }

    public static async Task<TestHostProcess> StartAsync(
        CancellationToken cancellationToken,
        params string[] additionalArguments)
    {
        var stopFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-testhost-stop-{Guid.NewGuid():N}.signal");
        var logDirectoryPath = Common.FrameworkTestEnvironment.CreateTestLogDirectory("testhost");
        var startInfo = Common.FrameworkTestEnvironment.CreateTestHostStartInfo(
            null,
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

        var host = new TestHostProcess(process, stopFilePath, logDirectoryPath);
        host._stdoutPump = PumpReaderAsync(
            process.StandardOutput,
            host._stdout,
            host._stdoutLogPath,
            host.TryHandleReadyLine);
        host._stderrPump = PumpReaderAsync(
            process.StandardError,
            host._stderr,
            host._stderrLogPath);
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
            File.Delete(_stopFilePath);
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
                if (_readyPayloadSource.Task.IsCompleted)
                {
                    var payload = await _readyPayloadSource.Task.WaitAsync(timeout.Token);
                    ReadyPayload = JsonDocument.Parse(payload);
                    return;
                }

                if (_process.HasExited)
                {
                    throw new InvalidOperationException(
                        $"Test host exited before readiness. Logs: {_logDirectoryPath}{Environment.NewLine}Stdout tail:{Environment.NewLine}{FormatTail(_stdout)}{Environment.NewLine}Stderr tail:{Environment.NewLine}{FormatTail(_stderr)}");
                }

                await Task.Delay(50, timeout.Token);
            }
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Timed out waiting for READY marker. Logs: {_logDirectoryPath}{Environment.NewLine}Stdout tail:{Environment.NewLine}{FormatTail(_stdout)}{Environment.NewLine}Stderr tail:{Environment.NewLine}{FormatTail(_stderr)}");
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
                $"Timed out waiting for test host exit after stdin EOF. Logs: {_logDirectoryPath}{Environment.NewLine}Stdout tail:{Environment.NewLine}{FormatTail(_stdout)}{Environment.NewLine}Stderr tail:{Environment.NewLine}{FormatTail(_stderr)}");
        }
    }

    private static async Task PumpReaderAsync(
        StreamReader reader,
        List<string> sink,
        string logPath,
        Action<string>? onLine = null)
    {
        await using var writer = new StreamWriter(
            new FileStream(logPath, FileMode.Create, FileAccess.Write, FileShare.Read));

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

            await writer.WriteLineAsync(line);
            await writer.FlushAsync();
            onLine?.Invoke(line);
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

        if (File.Exists(_stopFilePath))
        {
            File.Delete(_stopFilePath);
        }

        _process.Dispose();
    }

    private void TryHandleReadyLine(string line)
    {
        if (!line.StartsWith("READY:", StringComparison.Ordinal))
        {
            return;
        }

        var payload = line["READY:".Length..];

        try
        {
            using var document = JsonDocument.Parse(payload);

            if (document.RootElement.TryGetProperty("pid", out var pidElement)
                && pidElement.GetInt32() != _process.Id)
            {
                _readyPayloadSource.TrySetException(
                    new InvalidOperationException("READY marker PID does not match the launched process."));
                return;
            }

            _readyPayloadSource.TrySetResult(payload);
        }
        catch (Exception ex)
        {
            _readyPayloadSource.TrySetException(ex);
        }
    }

    private static string FormatTail(List<string> lines)
    {
        const int maxLines = 200;

        lock (lines)
        {
            if (lines.Count == 0)
            {
                return "<empty>";
            }

            return string.Join(
                Environment.NewLine,
                lines.Skip(Math.Max(0, lines.Count - maxLines)));
        }
    }
}
