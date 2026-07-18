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

    public bool Register(MeshOperationId operationId, CompletionHandler handler)
    {
        if (operationId == default) return false;
        _pending[operationId] = handler;
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
            handler(record, parts);
        else
            ZLinkMessageParts.DisposeAll(parts);
    }

    public void FailAll(RequestResult result)
    {
        foreach (var operationId in _pending.Keys.ToArray())
            if (_pending.TryRemove(operationId, out var handler))
                handler(
                    default,
                    Array.Empty<Message>());
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
