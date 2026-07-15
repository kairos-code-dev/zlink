using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Streams;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;

/// <summary>
/// The consoles currently watching. Node state changes arrive from runtime events and node
/// reports, which are not tied to any one session, so the push targets are kept here.
/// </summary>
public sealed class OpsConsoleRegistry
{
    private const int RecentAlertCount = 20;

    private readonly ConcurrentDictionary<string, IZLinkSessionContext> _consoles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, NodeStatusNotify> _latestNodes = new(StringComparer.Ordinal);
    private readonly Queue<NodeAlertNotify> _recentAlerts = new();
    private readonly object _nodeGate = new();
    private long _nodeVersion;

    /// <summary>
    /// Registers a console and gives a replacement stream session the latest node state. The
    /// same gate also protects node broadcasts: a session added before a state change receives
    /// that change, while a session added afterwards receives the cached result.
    /// </summary>
    public void Add(IZLinkSessionContext context)
    {
        while (true)
        {
            NodeStatusNotify[] snapshot;
            long version;
            lock (_nodeGate)
            {
                snapshot = _latestNodes.Values.ToArray();
                version = _nodeVersion;
            }

            foreach (var node in snapshot)
                Send(context, node);

            lock (_nodeGate)
            {
                // A broadcast that raced the replay did not yet know this session. Retry its
                // newest snapshot before publishing the session to future broadcasts.
                if (version != _nodeVersion) continue;
                _consoles[context.SessionId] = context;
                return;
            }
        }
    }

    /// <summary>
    /// Hands a console the alerts that arrived before it started watching. A node fault does
    /// not wait for an operator to be at the screen, and an alert nobody ever sees is an alert
    /// that, to the operator, did not happen.
    /// </summary>
    public void ReplayAlerts(IZLinkSessionContext context)
    {
        NodeAlertNotify[] backlog;
        lock (_recentAlerts) backlog = _recentAlerts.ToArray();

        foreach (var alert in backlog)
            Send(context, alert);
    }

    public void RecordAlert(NodeAlertNotify alert)
    {
        lock (_recentAlerts)
        {
            _recentAlerts.Enqueue(alert);
            while (_recentAlerts.Count > RecentAlertCount) _recentAlerts.Dequeue();
        }
    }

    public void Remove(IZLinkSessionContext context) =>
        ((ICollection<KeyValuePair<string, IZLinkSessionContext>>)_consoles).Remove(
            new KeyValuePair<string, IZLinkSessionContext>(context.SessionId, context));

    public void Broadcast(NodeStatusNotify message)
    {
        IZLinkSessionContext[] consoles;
        lock (_nodeGate)
        {
            _latestNodes[message.NodeId] = message;
            _nodeVersion++;
            consoles = _consoles.Values.ToArray();
        }

        SendAll(consoles, message);
    }

    public void Broadcast<TMessage>(TMessage message)
    {
        SendAll(_consoles.Values.ToArray(), message);
    }

    private void SendAll<TMessage>(
        IReadOnlyList<IZLinkSessionContext> consoles,
        TMessage message)
    {
        List<Exception>? failures = null;
        foreach (var console in consoles)
        {
            try
            {
                Send(console, message);
            }
            catch (Exception error)
            {
                Remove(console);
                (failures ??= []).Add(error);
            }
        }

        if (failures is not null)
            throw new AggregateException("One or more ops console pushes failed.", failures);
    }

    private static void Send<TMessage>(IZLinkSessionContext context, TMessage message) =>
        context.Client.Send(message).Submit();
}
