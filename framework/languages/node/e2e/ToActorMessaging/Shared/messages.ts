export const PacketNames = {
  actorNotify: 'ActorNotify',
  actorAsk: 'ActorAsk',
  actorPush: 'ActorPush',
  bindActor: 'BindActor'
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

export interface ActorPushReq {
  readonly scenario: string;
  readonly actorId: string;
  readonly value: string;
}

export interface ActorPushNotify {
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

export interface ActorRefSnapshot {
  readonly nodeRid: string;
  readonly actorId: string;
  readonly generation: string;
}

export interface ActorEnsureResponse {
  readonly actorId: string;
  readonly actor: ActorRefSnapshot;
}

export interface BindActorReq {
  readonly actor: ActorRefSnapshot;
}

export interface BindActorRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation: string;
  readonly boundCount: number;
}

export interface ActorCallRequest {
  readonly scenario: string;
  readonly actorId: string;
  readonly actor?: ActorRefSnapshot;
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

export function actorPush(scenario: string, actorId: string, value: string): ActorPushReq {
  return { scenario, actorId, value };
}
