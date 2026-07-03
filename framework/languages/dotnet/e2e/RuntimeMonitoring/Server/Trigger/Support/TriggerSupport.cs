using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;
using RuntimeMonitoring.Server.Trigger;

namespace RuntimeMonitoring.Server.Trigger.Support;

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _waiterGate = new();
    private readonly List<TaskCompletionSource> _waiters = new();

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        SignalWaiters();
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var waiter = AddWaiter();
            var snapshot = Snapshot();
            if (predicate(snapshot))
            {
                RemoveWaiter(waiter);
                return snapshot;
            }

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                RemoveWaiter(waiter);
                throw new TimeoutException("Timed out waiting for runtime monitoring evidence.");
            }

            try
            {
                await waiter.Task.WaitAsync(remaining, cancellationToken);
            }
            catch
            {
                RemoveWaiter(waiter);
                throw;
            }
        }
    }

    TaskCompletionSource AddWaiter()
    {
        var waiter = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_waiterGate)
        {
            _waiters.Add(waiter);
        }

        return waiter;
    }

    void RemoveWaiter(TaskCompletionSource waiter)
    {
        lock (_waiterGate)
        {
            _waiters.Remove(waiter);
        }
    }

    void SignalWaiters()
    {
        TaskCompletionSource[] waiters;
        lock (_waiterGate)
        {
            waiters = _waiters.ToArray();
            _waiters.Clear();
        }

        foreach (var waiter in waiters)
        {
            waiter.TrySetResult();
        }
    }
}

internal sealed record TriggerOptions(
    string HttpUrl,
    string ServiceChannelEndpoint,
    string ServiceBUrl,
    string ServiceBChannelEndpoint,
    string ThrowChannelEndpoint,
    string LogDir)
{
    public static TriggerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
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

            values[key] = args[++i];
        }

        return new TriggerOptions(
            Require(values, "--http-url"),
            Require(values, "--service-channel-endpoint"),
            Require(values, "--service-b-url"),
            Require(values, "--service-b-channel-endpoint"),
            Require(values, "--throw-channel-endpoint"),
            Require(values, "--log-dir"));
    }

    static string Require(IReadOnlyDictionary<string, string> values, string key) =>
        values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"{key} is required.");
}
