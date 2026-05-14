// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public interface SendOperation
{
    SendSubmitOperation Message(Message message);
}

public interface SendSubmitOperation
{
    SendSubmitOperation Message(Message message);
    SendSubmitOperation Flags(SendFlags flags);
    bool Submit();
}

public interface RequestOperation
{
    RequestSubmitOperation Message(Message message);
}

public interface RequestSubmitOperation
{
    RequestSubmitOperation Message(Message message);
    RequestSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(RequestCallback callback);
}

public interface RequestCallbackSubmitOperation
{
    RequestCallbackSubmitOperation Message(Message message);
    RequestCallbackSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    bool Submit(RequestCallback callback);
}

public interface ReplyOperation
{
    ReplySubmitOperation Message(Message message);
}

public interface ReplySubmitOperation
{
    ReplySubmitOperation Message(Message message);
    ReplySubmitOperation Flags(SendFlags flags);
    void Submit();
}
