export const PacketNames = {
  actorNotify: 'ActorNotify',
  actorAsk: 'ActorAsk'
} as const;

export interface ActorNotify {
  readonly scenario: string;
  readonly actorId: string;
  readonly value: string;
}

export interface ActorAsk {
  readonly scenario: string;
  readonly actorId: string;
  readonly value: string;
}

export interface ActorReply {
  readonly scenario: string;
  readonly actorId: string;
  readonly value: string;
}

export interface ActorEvidence {
  readonly scenario: string;
  readonly actorId: string;
  readonly kind: string;
  readonly value: string;
}

export interface ActorCallRequest {
  readonly scenario: string;
  readonly actorId: string;
  readonly value: string;
}

export interface ActorCallResponse {
  readonly scenario: string;
  readonly actorId: string;
  readonly result: string;
  readonly errorKind?: string;
}

export function actorNotify(scenario: string, actorId: string, value: string): ActorNotify {
  return { scenario, actorId, value };
}

export function actorAsk(scenario: string, actorId: string, value: string): ActorAsk {
  return { scenario, actorId, value };
}
