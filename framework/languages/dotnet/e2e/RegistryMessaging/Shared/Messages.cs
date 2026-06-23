namespace RegistryMessaging.Shared;

public sealed record ProfileRequest(string Value);

public sealed record ProfileReply(string Value, string ProviderRid);

public sealed record ProfileCommand(string CommandId);

public sealed record WorkflowRequest(string Value);

public sealed record WorkflowReply(string Value, string ProviderRid);

public sealed record ScenarioRoutePing(string Value);

public sealed record ScenarioRoutePong(
    string Value,
    string ProviderRid,
    string SourceRid);
