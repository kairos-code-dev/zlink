using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchRouter(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorSessionRegistry actorSessions,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext)
{
    private readonly ILogger _logger =
        runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkActorDispatchRouter>()
        ?? Microsoft.Extensions.Logging.Abstractions.NullLogger<ZLinkActorDispatchRouter>.Instance;
    private readonly ZLinkDispatchErrorReporter _dispatchErrors = new(
        runtime.Registration.DispatchOptions,
        runtime.Services,
        runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkActorDispatchRouter>());

    public async ValueTask SubmitByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await Async(actor, header, payload, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply> SubmitForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        return await SubmitForReplyAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask Async(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = actorSessions.GetOrCreate(actor.ActorId);
        var shouldPrune = false;
        ensureActorContext(actor, state);

        try
        {
            shouldPrune = await state.ExecuteDispatchAsync(
                    header,
                    ct => SubmitByCurrentLocationAsync(actor, state, header, payload, ct),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (shouldPrune)
            {
                actorSessions.TryRemove(actorId, state);
            }
        }
    }

    public async ValueTask NotifyDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await state.ExecuteLifecycleAsync(
                ct => NotifyDisconnectedByCurrentLocationAsync(actor, state, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorReply> SubmitForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        return await state.ExecuteDispatchAsync(
                header,
                ct => SubmitByCurrentLocationForReplyAsync(actor, state, header, payload, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<bool> SubmitByCurrentLocationAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: true),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is null)
        {
            var handled = await runtime.TrySubmitEntrySpotActorAsync(
                    actor,
                    state,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!handled)
            {
                ReportMissingHandler(
                    actor,
                    header,
                    ZLinkDispatchMessageKind.ActorSend,
                    ZLinkDispatchErrorAction.Drop);
            }
            return placement.Prune;
        }

        await placement.Activation.SubmitActorAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
        return placement.Prune;
    }

    private async ValueTask<ZLinkActorReply> SubmitByCurrentLocationForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: false),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is not null)
        {
            return await placement.Activation.SubmitActorForReplyAsync(
                    actor,
                    state,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var entryResult = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                actor,
                state,
                header,
                payload,
                cancellationToken)
            .ConfigureAwait(false);
        if (entryResult.Handled)
        {
            return entryResult.Reply
                ?? throw new InvalidOperationException(
                    $"Entry Spot actor request handler for '{header.Name}' returned no reply.");
        }

        var error = new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
            $"No Spot actor request handler is registered for '{header.Name}'.");
        ReportMissingHandler(
            actor,
            header,
            ZLinkDispatchMessageKind.ActorRequest,
            ZLinkDispatchErrorAction.ReplyError,
            error);
        throw error;
    }

    private async ValueTask NotifyDisconnectedByCurrentLocationAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        CancellationToken cancellationToken)
    {
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: false),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is not null)
        {
            await placement.Activation.NotifyActorDisconnectedAsync(actor, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await runtime.TryNotifyEntrySpotActorDisconnectedAsync(
                actor,
                targetNodeRid: null,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private void ReportMissingHandler(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        ZLinkDispatchMessageKind kind,
        ZLinkDispatchErrorAction action,
        Exception? exception = null)
    {
        var actionText = action == ZLinkDispatchErrorAction.ReplyError
            ? "reply-error"
            : "drop";
        var level = action == ZLinkDispatchErrorAction.ReplyError
            ? LogLevel.Error
            : LogLevel.Warning;
        var kindText = kind == ZLinkDispatchMessageKind.ActorRequest
            ? "ActorRequest"
            : "ActorSend";

        ZLinkMessageFlowLogger.HandlerMissing(
            _logger,
            level,
            "SpotActor",
            kindText,
            header.Name,
            actionText,
            "no-handler",
            actorId: actor.ActorId,
            actorType: actor.GetType().FullName);
        _dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
            ZLinkDispatchErrorSurface.SpotActor,
            kind,
            ZLinkDispatchErrorReason.HandlerMissing,
            action,
            header.Name,
            ActorId: actor.ActorId,
            CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString(),
            Exception: exception));
    }

}
