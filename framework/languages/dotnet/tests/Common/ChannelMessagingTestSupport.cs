using System.Collections.Concurrent;
using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.Tests;

internal static class ChannelMessagingTestSupport
{
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
        using var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
        listener.Start();
        return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
    }

    public static async Task StopHostsAsync(params IHost[] hosts)
    {
        foreach (var host in hosts)
        {
            await host.StopAsync();
        }

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        await Task.Delay(1000);
    }
}

public sealed class GetProfileRequest : IZLinkRequest<ProfileReply>
{
    public string UserId { get; init; } = string.Empty;
}

public sealed class ProfileReply
{
    public string Name { get; init; } = string.Empty;
}

public sealed class GetFilterOrderRequest : IZLinkRequest<FilterOrderReply>
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

public sealed class ProfileEventHandlers(ProfileEventRecorder recorder)
{
    [ZLinkEvent]
    public ValueTask OnInvalidatedAsync(
        ProfileInvalidated @event,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        Assert.Equal("profile.cache-invalidated", context.Topic);
        _ = cancellationToken;
        recorder.Events.Enqueue(@event.UserId);
        return ValueTask.CompletedTask;
    }
}

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
