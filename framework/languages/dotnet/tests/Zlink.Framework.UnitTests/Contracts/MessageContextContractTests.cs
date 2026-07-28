using System.Reflection;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.UnitTests;

public sealed class MessageContextContractTests
{
    private static readonly HashSet<string> RemovedContextTypeNames =
    [
        "ZLinkHandlerContext",
        "ZLinkRequestContext",
        "ZLinkSendContext",
        "ZLinkPublishContext",
        "ZLinkSpotActorRequestContext",
        "ZLinkSpotActorSendContext"
    ];

    [Fact]
    public void PublicSurface_ExposesOnlyUnifiedMessageContexts()
    {
        var properties = typeof(IZLinkMessageContext)
            .GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .ToDictionary(static property => property.Name, StringComparer.Ordinal);

        Assert.Equal(6, properties.Count);
        Assert.Equal(typeof(string), properties["MeshName"].PropertyType);
        Assert.Equal(typeof(string), properties["ChannelName"].PropertyType);
        Assert.Equal(typeof(string), properties["PacketName"].PropertyType);
        Assert.Equal(typeof(string), properties["ContentType"].PropertyType);
        Assert.Equal(typeof(ZLinkMessageMetadata), properties["Metadata"].PropertyType);
        Assert.Equal(typeof(string), properties["CorrelationId"].PropertyType);
        Assert.All(properties.Values, static property => Assert.Null(property.SetMethod));

        Assert.DoesNotContain(
            typeof(IZLinkMessageContext).Assembly.GetTypes(),
            static type => RemovedContextTypeNames.Contains(type.Name));
    }

    [Fact]
    public void HandlerFilter_ReceivesTheUnifiedMessageContextDirectly()
    {
        Assert.Equal(
            typeof(IZLinkMessageContext),
            typeof(IZLinkHandlerFilter)
                .GetMethod(nameof(IZLinkHandlerFilter.InvokeAsync))!
                .GetParameters()[0]
                .ParameterType);
        Assert.DoesNotContain(
            typeof(IZLinkHandlerFilter).Assembly.GetTypes(),
            static type => type.Name == "ZLinkHandlerInvocation");
    }

    [Fact]
    public void HandlerSignatures_UseExactMessageContexts()
    {
        Assert.Equal(
            typeof(IZLinkMessageContext),
            Parameters(typeof(IZLinkSendHandler<>))[1]);
        Assert.Equal(
            typeof(IZLinkMessageContext),
            Parameters(typeof(IZLinkRequestHandler<,>))[1]);
        Assert.Equal(
            typeof(ZLinkRouteMessageContext),
            Parameters(typeof(IZLinkRouteSendHandler<>))[1]);
        Assert.Equal(
            typeof(ZLinkRouteMessageContext),
            Parameters(typeof(IZLinkRouteRequestHandler<,>))[1]);
        Assert.Equal(
            typeof(ZLinkPublishMessageContext),
            Parameters(typeof(IZLinkSpotSubscriptionHandler<,>))[2]);

        AssertActorHandlerSignature(typeof(IZLinkSpotActorSendHandler<,,>), messageIndex: 3);
        AssertActorHandlerSignature(typeof(IZLinkSpotActorRequestHandler<,,,>), messageIndex: 3);
        AssertActorHandlerSignature(typeof(IZLinkEntrySpotActorSendHandler<,,>), messageIndex: 3);
        AssertActorHandlerSignature(typeof(IZLinkEntrySpotActorRequestHandler<,,,>), messageIndex: 3);
    }

    [Fact]
    public async Task Dispatcher_FilterAndHandlerShareExactMessageContext()
    {
        var probe = new FilterProbe();
        var registration = new ZLinkFrameworkRegistration();
        registration.Filters.Add(typeof(CapturingFilter));
        await using var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddSingleton<CapturingFilter>()
            .AddSingleton<FilteredRequestHandler>()
            .BuildServiceProvider();
        var dispatcher = new ZLinkHandlerDispatcher(
            services.GetRequiredService<IServiceScopeFactory>(),
            registration);
        var endpoint = ZLinkHandlerEndpointDescriptorFactory.CreateInterface(
            typeof(FilteredRequestHandler),
            typeof(IZLinkRequestHandler<FilterRequest, FilterReply>),
            ZLinkMessageKind.Request,
            new HashSet<string>(StringComparer.Ordinal),
            "channel-a",
            "filter.request");
        var metadata = new ZLinkMessageMetadata(
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["tenant"] = "alpha"
            });
        var context = new ZLinkMessageContext(
            "mesh-a",
            "channel-a",
            "filter.request",
            "application/json",
            metadata,
            "correlation-a");

        var result = Assert.IsType<FilterReply>(
            await dispatcher.DispatchAsync(
                endpoint,
                new FilterRequest("value"),
                context,
                CancellationToken.None));

        Assert.Equal("VALUE", result.Value);
        Assert.Same(context, probe.FilterContext);
        var handlerContext = Assert.IsAssignableFrom<IZLinkMessageContext>(probe.HandlerContext);
        Assert.Same(context, handlerContext);
        Assert.Equal("alpha", handlerContext.Metadata.Find("tenant"));
        Assert.Equal("correlation-a", handlerContext.CorrelationId);
    }

    private static Type[] Parameters(Type handlerType)
    {
        return handlerType
            .GetMethod("HandleAsync")!
            .GetParameters()
            .Select(static parameter => parameter.ParameterType)
            .ToArray();
    }

    private static void AssertActorHandlerSignature(Type handlerType, int messageIndex)
    {
        var genericArguments = handlerType.GetGenericArguments();
        var parameters = Parameters(handlerType);

        Assert.Equal(genericArguments[0], parameters[0]);
        Assert.Equal(genericArguments[1], parameters[1]);
        Assert.Equal(typeof(IZLinkMessageContext), parameters[2]);
        Assert.Equal(genericArguments[2], parameters[messageIndex]);
        Assert.Equal(typeof(CancellationToken), parameters[^1]);
    }

    private sealed record FilterRequest(string Value);

    private sealed record FilterReply(string Value);

    private sealed class FilterProbe
    {
        public IZLinkMessageContext? FilterContext { get; set; }

        public IZLinkMessageContext? HandlerContext { get; set; }
    }

    private sealed class CapturingFilter(FilterProbe probe) : IZLinkHandlerFilter
    {
        public async ValueTask InvokeAsync(
            IZLinkMessageContext context,
            ZLinkHandlerFilterNext next,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            probe.FilterContext = context;
            await next();
        }
    }

    private sealed class FilteredRequestHandler(FilterProbe probe)
        : IZLinkRequestHandler<FilterRequest, FilterReply>
    {
        public ValueTask<FilterReply> HandleAsync(
            FilterRequest request,
            IZLinkMessageContext context,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            probe.HandlerContext = context;
            return ValueTask.FromResult(
                new FilterReply(request.Value.ToUpperInvariant()));
        }
    }
}
