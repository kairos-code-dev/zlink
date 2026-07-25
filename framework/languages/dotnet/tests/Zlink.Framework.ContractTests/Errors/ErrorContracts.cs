using System.Reflection;
using Zlink.Framework.Contracts.Errors;

namespace Zlink.Framework.ContractTests.Errors;

public sealed class ErrorContracts
{
    [Fact]
    public void Framework_exception_contract_matches_the_frozen_surface()
    {
        Assert.Equal(
            new Dictionary<string, int>(StringComparer.Ordinal)
            {
                [nameof(ZLinkFrameworkErrorKind.ActorRouteNotFound)] = 0,
                [nameof(ZLinkFrameworkErrorKind.ActorCreateFailed)] = 1,
                [nameof(ZLinkFrameworkErrorKind.ActorAlreadyExists)] = 2,
                [nameof(ZLinkFrameworkErrorKind.ActorTypeMismatch)] = 3,
                [nameof(ZLinkFrameworkErrorKind.SpotCreateFailed)] = 4,
                [nameof(ZLinkFrameworkErrorKind.SpotRouteNotFound)] = 5,
                [nameof(ZLinkFrameworkErrorKind.SpotTypeMismatch)] = 6,
                [nameof(ZLinkFrameworkErrorKind.ActorSessionNotBound)] = 7,
                [nameof(ZLinkFrameworkErrorKind.HandlerNotFound)] = 8,
                [nameof(ZLinkFrameworkErrorKind.RouteHandlerNotFound)] = 9,
                [nameof(ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound)] = 10,
                [nameof(ZLinkFrameworkErrorKind.PayloadDecodeFailed)] = 11,
                [nameof(ZLinkFrameworkErrorKind.RouteNotConnected)] = 12,
                [nameof(ZLinkFrameworkErrorKind.RequestTargetNotFound)] = 13,
                [nameof(ZLinkFrameworkErrorKind.RequestRejected)] = 14,
                [nameof(ZLinkFrameworkErrorKind.RequestProtocolError)] = 15,
                [nameof(ZLinkFrameworkErrorKind.RequestFailed)] = 16,
                [nameof(ZLinkFrameworkErrorKind.WorkerQueueFull)] = 17,
                [nameof(ZLinkFrameworkErrorKind.WorkerTimedOut)] = 18,
                [nameof(ZLinkFrameworkErrorKind.WorkerFailed)] = 19,
                [nameof(ZLinkFrameworkErrorKind.ActorLocationStale)] = 20,
                [nameof(ZLinkFrameworkErrorKind.ActorCreateRejected)] = 21,
                [nameof(ZLinkFrameworkErrorKind.ObjectClientNotConfigured)] = 22,
                [nameof(ZLinkFrameworkErrorKind.MeshSelectionRequired)] = 23,
                [nameof(ZLinkFrameworkErrorKind.MeshNotFound)] = 24,
                [nameof(ZLinkFrameworkErrorKind.InvalidConfiguration)] = 25,
                [nameof(ZLinkFrameworkErrorKind.AlreadySubmitted)] = 26,
                [nameof(ZLinkFrameworkErrorKind.ActorGenerationStale)] = 27,
                [nameof(ZLinkFrameworkErrorKind.ActorMoving)] = 28,
                [nameof(ZLinkFrameworkErrorKind.DeadlineExceeded)] = 29,
                [nameof(ZLinkFrameworkErrorKind.PlacementCapacityExhausted)] = 30,
                [nameof(ZLinkFrameworkErrorKind.RoutingIdConflict)] = 31,
                [nameof(ZLinkFrameworkErrorKind.SpotGenerationStale)] = 32,
                [nameof(ZLinkFrameworkErrorKind.SpotMoving)] = 33,
                [nameof(ZLinkFrameworkErrorKind.RelocationDataLost)] = 34,
                [nameof(ZLinkFrameworkErrorKind.SpotIdConflict)] = 35,
                [nameof(ZLinkFrameworkErrorKind.RuntimeShutdown)] = 36,
                [nameof(ZLinkFrameworkErrorKind.RelocationDisabled)] = 37,
                [nameof(ZLinkFrameworkErrorKind.RelocationTargetUnavailable)] = 38,
                [nameof(ZLinkFrameworkErrorKind.RelocationFailed)] = 39
            },
            Enum.GetValues<ZLinkFrameworkErrorKind>()
                .ToDictionary(static value => value.ToString(), static value => (int)value, StringComparer.Ordinal));

        var constructor = Assert.Single(typeof(ZLinkFrameworkException).GetConstructors());
        var parameters = constructor.GetParameters();
        Assert.Equal(
            [
                typeof(ZLinkFrameworkErrorKind),
                typeof(string),
                typeof(bool?),
                typeof(Exception)
            ],
            parameters.Select(static parameter => parameter.ParameterType));
        Assert.False(parameters[0].HasDefaultValue);
        Assert.False(parameters[1].HasDefaultValue);
        Assert.Null(parameters[2].DefaultValue);
        Assert.Null(parameters[3].DefaultValue);

        var nullability = new NullabilityInfoContext();
        Assert.Equal(NullabilityState.NotNull, nullability.Create(parameters[1]).ReadState);
        Assert.Equal(NullabilityState.Nullable, nullability.Create(parameters[3]).ReadState);
        Assert.Equal(typeof(ZLinkFrameworkErrorKind),
            typeof(ZLinkFrameworkException).GetProperty(nameof(ZLinkFrameworkException.Kind))!.PropertyType);
        Assert.Equal(typeof(bool),
            typeof(ZLinkFrameworkException).GetProperty(nameof(ZLinkFrameworkException.IsRetriable))!.PropertyType);
    }
}
