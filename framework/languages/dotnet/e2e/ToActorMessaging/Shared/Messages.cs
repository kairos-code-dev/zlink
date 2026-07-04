namespace ToActorMessaging.Shared;

public sealed record ActorNotify(string Scenario, string ActorId, string Value);

public sealed record ActorAsk(string Scenario, string ActorId, string Value);

public sealed record ActorReply(string Scenario, string ActorId, string Value);

public sealed record ActorEvidence(string Scenario, string ActorId, string Kind, string Value);

public sealed record ActorCallRequest(string Scenario, string ActorId, string Value);

public sealed record ActorCallResponse(string Scenario, string ActorId, string Result, string? ErrorKind = null);
