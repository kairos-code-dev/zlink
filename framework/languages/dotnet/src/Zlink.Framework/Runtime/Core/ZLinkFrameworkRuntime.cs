using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal readonly record struct CreateActorResult(
    IZLinkActor Actor,
    bool Created);

internal sealed class ZLinkFrameworkRuntime
{
    private readonly IServiceProvider _services;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkRegistryRuntime? _registryRuntime;
    private readonly ZLinkChannelRuntimeManager _channels;
    private readonly ZLinkStreamRuntimeManager _streams;
    private readonly ZLinkSpotRuntimeManager _spots;
    private readonly ZLinkFrameworkRuntimeStateFactory _stateFactory;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkActorSessionManager _actorSessionManager;
    private readonly ZLinkFrameworkActorFacade _actors;
    private readonly ZLinkSessionActorBindingTable _sessionActorBindings = new();
    private ZLinkFrameworkRuntimeState? _state;

    public ZLinkFrameworkRuntime(
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkRegistryRuntime? registryRuntime = null)
    {
        _services = services;
        _registration = registration;
        _registryRuntime = registryRuntime;
        _channels = new ZLinkChannelRuntimeManager(
            services,
            backendAdapterFactory,
            registration,
            new ZLinkChannelMessagePump(handlerRegistry, dispatcher, registration));
        _streams = new ZLinkStreamRuntimeManager(services, backendAdapterFactory, registration);
        _spots = new ZLinkSpotRuntimeManager(services, this, backendAdapterFactory, registration);
        _stateFactory = new ZLinkFrameworkRuntimeStateFactory(
            backendAdapterFactory,
            registration,
            _channels,
            _streams,
            _spots);
        _actorSessionManager = new ZLinkActorSessionManager(this, services, GetActorSpotNode);
        _actors = new ZLinkFrameworkActorFacade(
            registration,
            _spots,
            _actorSessionManager,
            GetOrStartState,
            GetActorSpotNode);
    }

    public IZLinkBackendContext? Context => _state?.Context;

    public ZLinkFrameworkRegistration Registration => _registration;

    internal IServiceProvider Services => _services;

    internal IZLinkRouteClient RouteClient => _services.GetRequiredService<IZLinkRouteClient>();

    public bool IsStarted => _state is not null;

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        if (_registryRuntime is not null)
        {
            await _registryRuntime.StartAsync(cancellationToken);
        }

        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_state is not null)
            {
                return;
            }

            _state = await _stateFactory.CreateAsync().ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        ZLinkFrameworkRuntimeState? stateToDispose;

        await _gate.WaitAsync(cancellationToken);
        try
        {
            stateToDispose = _state;
            _state = null;
        }
        finally
        {
            _gate.Release();
        }

        if (stateToDispose is not null)
        {
            await stateToDispose.DisposeAsync();
        }

        if (_registryRuntime is not null)
        {
            await _registryRuntime.StopAsync(cancellationToken);
        }
    }

    internal ZLinkChannelRuntimeBundle GetOrCreateClientBundle(string channelName)
    {
        var state = GetOrStartState();
        return _channels.GetOrCreateClientBundle(state, channelName);
    }

    internal ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(string channelName)
    {
        var state = GetOrStartState();
        return _channels.GetOrCreatePublisherBundle(state, channelName);
    }

    internal ZLinkRouteChannelRuntime GetRouteChannel(string routerChannelId)
    {
        var state = GetOrStartState();
        return _channels.GetRouteChannel(state, routerChannelId);
    }

    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        var state = GetOrStartState();
        return _spots.GetPublisherBundle(state, channelName);
    }

    internal async ValueTask<ZLinkSpotCreateResult> CreateSpotAsync(
        string spotName,
        RoutingId? spotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.CreateAsync(GetOrStartState(), spotName, spotRid, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotInfo?> GetSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.GetAsync(GetOrStartState(), spotRid, cancellationToken);
    }

    internal async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListSpotsAsync(
        CancellationToken cancellationToken)
    {
        return await _spots.ListAsync(GetOrStartState(), cancellationToken);
    }

    internal async ValueTask<bool> RemoveSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.RemoveAsync(GetOrStartState(), spotRid, cancellationToken);
    }

    internal IZLinkBackendSpotNode? GetActorSpotNode()
    {
        return _state?.SpotNodes.Values.FirstOrDefault()?.Node;
    }

    internal async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return await _actors.JoinActorAsync<TRequest, TReply>(
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    internal async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.JoinActorToSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.LeaveActorFromSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.AttachActorAsync(actor, stream, cancellationToken);

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.DisconnectActorAsync(actor, stream, cancellationToken);

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorAsync(actor, header, body, cancellationToken);

    internal async ValueTask<bool> TrySubmitEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return await _spots.TrySubmitEntrySpotActorAsync(
                state,
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitEntrySpotActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return await _spots.TrySubmitEntrySpotActorForReplyAsync(
                state,
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorJoinedAsync(
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorJoinedAsync(
                _state,
                actor,
                info,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorLeftAsync(
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorLeftAsync(
                _state,
                actor,
                info,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
        => await _actors.CreateLocalActorAsync(actorId, actorType, cancellationToken);

    internal async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorForReplyAsync(actorId, header, body, cancellationToken);

    internal async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorByIdAsync(actorId, header, body, cancellationToken);

    internal ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
        => _actors.GetOrCreateActorState(actorId);

    internal string ResolveDefaultRouterChannelId()
    {
        if (_registration.RouteChannels.Count != 1)
        {
            throw new InvalidOperationException("Exactly one routed channel is required for session actor dispatch.");
        }

        return _registration.RouteChannels.Keys.Single();
    }

    internal ZLinkActorRoute ResolveLocalActorRoute()
    {
        var routerChannelId = ResolveDefaultRouterChannelId();
        return new ZLinkActorRoute(routerChannelId, ResolveSessionRouterId(routerChannelId));
    }

    internal RoutingId ResolveSessionRouterId(string routerChannelId)
    {
        if (!_registration.RouteChannels.TryGetValue(routerChannelId, out var routed)
            || routed.RoutingOptions.RoutingId.Size == 0)
        {
            throw new InvalidOperationException($"Route channel '{routerChannelId}' must configure a routing id.");
        }

        return routed.RoutingOptions.RoutingId;
    }

    internal void BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionActorBindings.Bind(actorId, context, bindingToken);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionActorBindings.Unbind(actorId, context, bindingToken);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _sessionActorBindings.TryGet(actorId, bindingToken, out context);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotRouterConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _spots.GetRouterConnections(state, spotNodeName);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPubSubConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _spots.GetPubSubConnections(state, spotNodeName);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotChannelClientConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _spots.GetChannelClientConnections(state, spotNodeName, channelName);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPublisherConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _spots.GetPublisherConnections(state, spotNodeName, channelName);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetClientConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _channels.GetClientConnections(state, channelName);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSubscriberConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        return _channels.GetSubscriberConnections(state, channelName);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        var state = GetOrStartState();
        return _channels.GetMonitoringSocket(state, sourceName);
    }

    internal ZLinkSpotMonitoringSnapshot GetSpotMonitoringSnapshot(string spotNodeName)
    {
        return _spots.GetMonitoringSnapshot(GetOrStartState(), spotNodeName);
    }

    internal ZLinkSpotNodeRuntime GetSpotNodeRuntime(string spotNodeName)
    {
        var state = GetOrStartState();
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new InvalidOperationException($"SPOT node '{spotNodeName}' is not registered.");
    }

    private async ValueTask<ZLinkFrameworkRuntimeState> GetStartedStateAsync(
        CancellationToken cancellationToken)
    {
        if (_state is null)
        {
            await StartAsync(cancellationToken);
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

    private ZLinkFrameworkRuntimeState GetOrStartState()
    {
        if (_state is null)
        {
            throw new InvalidOperationException(
                "ZLink framework runtime is not started. Call StartAsync before using synchronous runtime APIs.");
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

}
