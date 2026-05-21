using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.Tests;

internal static class ChannelMessagingTestSupport
{
    private static readonly TestPortAllocator Ports = new();

    public static async Task<T> ExecuteWithRetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        int attempts = 20,
        int delayMs = 100)
    {
        Exception? lastError = null;

        for (var attempt = 0; attempt < attempts; attempt++)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(delayMs);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("ZLink integration retry timed out.");
    }

    public static int GetEphemeralPort()
    {
        return Ports.Reserve();
    }

    public static string GetTcpEndpoint()
        => $"tcp://127.0.0.1:{GetEphemeralPort()}";

    public static async Task StopHostsAsync(params IHost[] hosts)
    {
        foreach (var host in hosts)
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await host.StopAsync(timeout.Token);
            }
            catch (OperationCanceledException ex) when (timeout.IsCancellationRequested)
            {
                throw new TimeoutException($"Timed out while stopping host {host.GetType().FullName}.", ex);
            }
        }

    }

    public static async Task RunWithHostCleanupAsync(
        Func<Task> runAsync,
        params IHost[] hosts)
    {
        Exception? runFailure = null;
        try
        {
            await runAsync();
        }
        catch (Exception ex)
        {
            runFailure = ex;
        }

        try
        {
            await StopHostsAsync(hosts);
        }
        catch (Exception cleanupFailure) when (runFailure is not null)
        {
            throw new AggregateException(
                "Test failed, and host cleanup also failed.",
                runFailure,
                cleanupFailure);
        }

        if (runFailure is not null)
        {
            ExceptionDispatchInfo.Capture(runFailure).Throw();
        }
    }
}

internal sealed class TestPortAllocator
{
    private const int MinPort = 1024;
    private const int MaxPort = 65535;
    private const int PortCount = MaxPort - MinPort + 1;

    private readonly ConcurrentDictionary<int, byte> _allocatedPorts = new();
    private readonly string _reservationDirectory = Path.Combine(
        Path.GetTempPath(),
        "zlink-framework-test-ports");
    private readonly object _cleanupGate = new();
    private int _nextOffset = Environment.ProcessId % PortCount;
    private bool _cleanupCompleted;

    public int Reserve()
    {
        EnsureStaleReservationsRemoved();

        for (var attempt = 0; attempt < PortCount; attempt++)
        {
            var port = NextPort();
            if (!_allocatedPorts.TryAdd(port, 0))
            {
                continue;
            }

            if (!TryReserveForCurrentProcess(port))
            {
                continue;
            }

            if (CanBindLoopback(port))
            {
                return port;
            }
        }

        throw new InvalidOperationException(
            $"No loopback TCP port could be reserved for the test process after scanning {PortCount} ports.");
    }

    private int NextPort()
    {
        var offset = Interlocked.Increment(ref _nextOffset);
        return MinPort + (offset % PortCount);
    }

    private void EnsureStaleReservationsRemoved()
    {
        if (_cleanupCompleted)
        {
            return;
        }

        lock (_cleanupGate)
        {
            if (_cleanupCompleted)
            {
                return;
            }

            Directory.CreateDirectory(_reservationDirectory);
            foreach (var path in Directory.EnumerateFiles(_reservationDirectory, "*.lock"))
            {
                DeleteIfStale(path);
            }

            _cleanupCompleted = true;
        }
    }

    private void DeleteIfStale(string path)
    {
        if (!TryReadProcessId(path, out var processId))
        {
            TryDelete(path);
            return;
        }

        if (processId == Environment.ProcessId)
        {
            return;
        }

        try
        {
            _ = System.Diagnostics.Process.GetProcessById(processId);
        }
        catch (ArgumentException)
        {
            TryDelete(path);
        }
        catch (InvalidOperationException)
        {
            TryDelete(path);
        }
    }

    private static bool TryReadProcessId(string path, out int processId)
    {
        processId = 0;
        try
        {
            var text = File.ReadAllText(path).Trim();
            return int.TryParse(text, out processId);
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private bool TryReserveForCurrentProcess(int port)
    {
        var reservationPath = Path.Combine(_reservationDirectory, $"{port}.lock");
        try
        {
            using var reservation = new FileStream(
                reservationPath,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.Read);
            using var writer = new StreamWriter(reservation);
            writer.Write(Environment.ProcessId);
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool CanBindLoopback(int port)
    {
        try
        {
            using var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, port);
            listener.Start();
            return true;
        }
        catch (System.Net.Sockets.SocketException)
        {
            return false;
        }
    }
}

public sealed class GetProfileRequest
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileReply
{
    public string Name { get; init; } = string.Empty;
}

public sealed class GetFilterOrderRequest
{
}

public sealed record FilterOrderReply
{
    public IReadOnlyList<string> Sequence { get; init; } = [];
}

public sealed class RefreshProfileCacheCommand
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileInvalidated
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileCommandRecorder
{
    public ConcurrentQueue<string> Commands { get; } = [];
}

public sealed class ProfileEventRecorder
{
    public ConcurrentQueue<string> Events { get; } = [];
}

public sealed class FilterOrderRecorder
{
    public ConcurrentQueue<string> Entries { get; } = [];
}

[ZLinkHandlerGroup("profile")]
public sealed class ProfileHandlers(ProfileCommandRecorder recorder)
{
    [ZLinkRequest]
    public ValueTask<ProfileReply> GetAsync(
        GetProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        return ValueTask.FromResult(new ProfileReply
        {
            Name = $"user:{request.UserId}",
        });
    }

    [ZLinkSend]
    public ValueTask RefreshAsync(
        RefreshProfileCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        recorder.Commands.Enqueue(command.UserId);
        return ValueTask.CompletedTask;
    }
}

[ZLinkHandlerGroup("profile-events")]
public sealed class ProfileEventHandlers(ProfileEventRecorder recorder)
{
    [ZLinkPublish]
    public ValueTask OnInvalidatedAsync(
        ProfileInvalidated @event,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("profile.cache-invalidated", context.Topic);
        _ = cancellationToken;
        recorder.Events.Enqueue(@event.UserId);
        return ValueTask.CompletedTask;
    }
}

[ZLinkHandlerGroup("filter-order")]
public sealed class FilterOrderHandlers(FilterOrderRecorder recorder)
{
    [ZLinkRequest]
    public ValueTask<FilterOrderReply> GetAsync(
        GetFilterOrderRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        _ = cancellationToken;
        recorder.Entries.Enqueue("handler");
        return ValueTask.FromResult(new FilterOrderReply
        {
            Sequence = recorder.Entries.ToArray(),
        });
    }
}

public sealed class OuterOrderFilter(FilterOrderRecorder recorder) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        _ = invocation;
        recorder.Entries.Enqueue("outer:before");
        var result = await next(cancellationToken);
        recorder.Entries.Enqueue("outer:after");
        return result is FilterOrderReply reply
            ? reply with { Sequence = recorder.Entries.ToArray() }
            : result;
    }
}

public sealed class InnerOrderFilter(FilterOrderRecorder recorder) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        _ = invocation;
        recorder.Entries.Enqueue("inner:before");
        var result = await next(cancellationToken);
        recorder.Entries.Enqueue("inner:after");
        return result is FilterOrderReply reply
            ? reply with { Sequence = recorder.Entries.ToArray() }
            : result;
    }
}
