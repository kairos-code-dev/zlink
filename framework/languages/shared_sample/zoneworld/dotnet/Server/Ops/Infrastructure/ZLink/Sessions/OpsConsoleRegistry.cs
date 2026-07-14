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
    private readonly Queue<NodeAlertNotify> _recentAlerts = new();

    public void Add(IZLinkSessionContext context) => _consoles[context.SessionId] = context;

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
        {
            try
            {
                context.Client.Send(alert).Submit();
            }
            catch
            {
                // The console went away between the request and this replay.
            }
        }
    }

    public void RecordAlert(NodeAlertNotify alert)
    {
        lock (_recentAlerts)
        {
            _recentAlerts.Enqueue(alert);
            while (_recentAlerts.Count > RecentAlertCount) _recentAlerts.Dequeue();
        }
    }

    public void Remove(IZLinkSessionContext context) => _consoles.TryRemove(context.SessionId, out _);

    public void Broadcast<TMessage>(TMessage message)
    {
        foreach (var console in _consoles.Values)
        {
            try
            {
                console.Client.Send(message).Submit();
            }
            catch
            {
                // A console that went away between the lookup and the send is not an error;
                // its OnDisconnected removes it.
            }
        }
    }
}
