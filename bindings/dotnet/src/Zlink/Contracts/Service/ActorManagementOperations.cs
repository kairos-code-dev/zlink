// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public sealed record ActorLookupResult(RequestResult Result, ActorRef Actor,
    uint Flags);

public delegate void ActorLookupHandler(ActorLookupResult result);

public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

public interface ActorLeaveOperation
{
    ActorLeaveOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorDestroyOperation
{
    ActorDestroyOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorLookupOperation
{
    ActorLookupOperation Timeout(TimeSpan timeout);
    Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default);
    bool Submit(ActorLookupHandler callback);
}

public interface ActorBindOperation
{
    ActorBindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorUnbindOperation
{
    ActorUnbindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}
