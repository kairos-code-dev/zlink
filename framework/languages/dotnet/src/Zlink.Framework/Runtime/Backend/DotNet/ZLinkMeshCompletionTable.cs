using System.Collections.Concurrent;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Backend.DotNet;

// RouteMesh 10.0.0 request/reply bridge.
//
// 9.x request operations took a callback and the binding drove it when the reply
// arrived. 10.0.0 requests are pull dispatch: RequestTo*/Join*/Destroy* return a
// SubmitResult plus an out MeshOperationId, and the reply/terminal outcome is
// delivered later as a Completion receive record. This table holds a pending
// record-aware completion handler keyed by MeshOperationId; the node dispatch pump
// resolves the entry when the matching Completion record drains. Handlers close
// over the framework callback shape they need (RequestCallback, ActorJoinCallback,
// ActorJoinEntrySpotCallback, or a TaskCompletionSource for async waits).
internal sealed class ZLinkMeshCompletionTable
{
    internal delegate void CompletionHandler(
        MeshReceiveRecord record, IReadOnlyList<Message> parts);

    private readonly ConcurrentDictionary<MeshOperationId, CompletionHandler> _pending = new();

    // A same-process completion can drain on the pump thread between the
    // operation submit and the caller's Register. Dropping it would leave the
    // awaiting caller hanging forever, so unmatched completions are buffered
    // until their handler registers. Records are fully managed views (the
    // Core-owned parts were retained by the pump before Complete), so holding
    // them is safe; the bound guards a leak from operations nobody awaits.
    private const int MaxEarlyCompletions = 4096;
    private readonly ConcurrentDictionary<
        MeshOperationId, (MeshReceiveRecord Record, IReadOnlyList<Message> Parts)> _early = new();

    public bool Register(MeshOperationId operationId, CompletionHandler handler)
    {
        if (operationId == default) return false;
        if (_early.TryRemove(operationId, out var early))
        {
            handler(early.Record, early.Parts);
            return true;
        }

        _pending[operationId] = handler;
        // Complete may have buffered between the early check and the pending
        // publication; whichever side observes both entries delivers.
        if (_early.TryRemove(operationId, out early)
            && _pending.TryRemove(operationId, out var raced))
            raced(early.Record, early.Parts);
        return true;
    }

    // Registers a plain request callback (result + reply parts).
    public bool RegisterRequest(MeshOperationId operationId, RequestCallback callback)
    {
        return Register(operationId, (record, parts) =>
            callback(MapResult(record.TerminalResult, record.FailureErrno), parts));
    }

    public void Complete(
        MeshReceiveRecord record, IReadOnlyList<Message> parts)
    {
        if (_pending.TryRemove(record.OperationId, out var handler))
        {
            handler(record, parts);
            return;
        }

        if (_early.Count >= MaxEarlyCompletions)
        {
            ZLinkMessageParts.DisposeAll(parts);
            return;
        }

        _early[record.OperationId] = (record, parts);
        // Register may have published a handler between the pending check and
        // the early publication; whichever side observes both entries delivers.
        if (_pending.TryRemove(record.OperationId, out handler)
            && _early.TryRemove(record.OperationId, out var raced))
            handler(raced.Record, raced.Parts);
    }

    public void FailAll(RequestResult result)
    {
        foreach (var operationId in _pending.Keys.ToArray())
            if (_pending.TryRemove(operationId, out var handler))
                handler(
                    default,
                    Array.Empty<Message>());
        foreach (var operationId in _early.Keys.ToArray())
            if (_early.TryRemove(operationId, out var early))
                ZLinkMessageParts.DisposeAll(early.Parts);
    }

    // Maps a Completion receive record's terminal result/errno onto the framework
    // RequestResult surface the callbacks expect.
    public static RequestResult MapResult(int terminalResult, int failureErrno)
    {
        if (terminalResult == 0) return RequestResult.Ok;
        return (SubmitResult)terminalResult switch
        {
            SubmitResult.Ok => RequestResult.Ok,
            SubmitResult.NotFound => RequestResult.NotFound,
            SubmitResult.NotConnected => RequestResult.NotConnected,
            SubmitResult.NotAdmitted => RequestResult.NotConnected,
            SubmitResult.Terminated => RequestResult.Terminated,
            SubmitResult.InvalidArgument => RequestResult.InvalidArgument,
            SubmitResult.InvalidState => RequestResult.InvalidState,
            SubmitResult.NotSupported => RequestResult.NotSupported,
            SubmitResult.OutOfMemory => RequestResult.InternalError,
            SubmitResult.Backpressured => RequestResult.Busy,
            _ => RequestResult.InternalError
        };
    }
}
