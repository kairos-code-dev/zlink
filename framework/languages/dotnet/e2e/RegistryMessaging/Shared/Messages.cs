namespace RegistryMessaging.Shared;

public sealed record ProfileRequest(string Value);

public sealed record ProfileReply(string Value, string ProviderRid);

public sealed record DynamicProfileRequest(string RegistryRouterEndpoint, string Value, int ExpectedReady = 1);

public sealed record DynamicProfileBatchRequest(
    string RegistryRouterEndpoint,
    string[] Values,
    int ExpectedReady = 1);

public sealed record ProfileCommand(string CommandId);

public sealed record EvidenceWaitRequest(string Contains, int TimeoutMilliseconds = 10000);

public sealed record PayloadRequest(string Marker, string Payload);

public sealed record PayloadReply(string Marker, int Length, string Sha256);

public sealed record WorkflowRequest(string Value);

public sealed record WorkflowReply(string Value, string ProviderRid);

public sealed record ScenarioRoutePing(string Value);

public sealed record ScenarioRoutePong(
    string Value,
    string ProviderRid,
    string SourceRid);

public sealed record RouteMissingResult(bool Failed);

public sealed record RequestFailureResult(bool Failed, string FailureType);
