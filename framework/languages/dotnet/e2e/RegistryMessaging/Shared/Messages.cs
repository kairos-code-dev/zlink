namespace RegistryMessaging.Shared;

public sealed record ProfileReq(string Value);

public sealed record ProfileRes(string Value, string ProviderRid);

public sealed record DynamicProfileReq(string RegistryRouterEndpoint, string Value, int ExpectedReady = 1);

public sealed record DynamicProfileBatchReq(
    string RegistryRouterEndpoint,
    string[] Values,
    int ExpectedReady = 1);

public sealed record ProfileMsg(string CommandId);

public sealed record EvidenceWaitReq(string Contains, int TimeoutMilliseconds = 10000);

public sealed record PayloadReq(string Marker, string Payload);

public sealed record PayloadRes(string Marker, int Length, string Sha256);

public sealed record WorkflowReq(string Value);

public sealed record WorkflowRes(string Value, string ProviderRid);

public sealed record ScenarioRoutePing(string Value);

public sealed record ScenarioRoutePong(
    string Value,
    string ProviderRid,
    string SourceRid);

public sealed record RouteMissingRes(bool Failed);

public sealed record RequestFailureRes(bool Failed, string FailureType);