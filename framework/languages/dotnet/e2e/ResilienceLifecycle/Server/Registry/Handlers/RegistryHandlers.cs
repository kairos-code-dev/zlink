using ResilienceLifecycle.Shared;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Endpoints;
using ResilienceLifecycle.Server.Registry.Infrastructure;
using ResilienceLifecycle.Server.Registry;

namespace ResilienceLifecycle.Server.Registry.Handlers;

internal sealed class ProfileRequestHandler(EvidenceStore evidence, FaultState fault)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public async ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (fault.Mode == "gray" && request.Value == "gray")
        {
            evidence.Add($"profile-fault|rid={evidence.Rid}|marker={request.Marker}|mode=gray");
            throw new InvalidOperationException("gray failure");
        }

        if (request.Value == "slow")
        {
            evidence.Add($"profile-start|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
            await Task.Delay(TimeSpan.FromMilliseconds(700), cancellationToken);
        }

        evidence.Add($"profile-request|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
        return new ProfileReply($"profile:{request.Value}", evidence.Rid, request.Marker);
    }
}

internal sealed class ProfileCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ProfileCommand>
{
    public ValueTask HandleAsync(
        ProfileCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"profile-command|rid={evidence.Rid}|marker={command.Marker}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence, FaultState fault)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != ZLinkMessageFlowOutcome.Error)
        {
            return ValueTask.CompletedTask;
        }

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        if (fault.Mode == "observer-throws")
        {
            throw new InvalidOperationException("dispatch observer failure");
        }

        return ValueTask.CompletedTask;
    }
}
