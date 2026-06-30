export const PubSubNames = {
  channel: 'events',
  mainTopic: 'orders',
  otherTopic: 'billing'
} as const;

export const PacketNames = {
  eventMsg: 'EventMsg',
  missingEventMsg: 'MissingEventMsg'
} as const;

export interface EventMsg {
  readonly runId: string;
  readonly sequence: number;
  readonly value: string;
}

export interface MissingEventMsg {
  readonly runId: string;
  readonly sequence: number;
  readonly value: string;
}

export interface EvidenceWaitReq {
  readonly containsAll?: readonly string[];
  readonly containsAnyGroups?: readonly (readonly string[])[];
  readonly containsAllLineGroups?: readonly (readonly string[])[];
  readonly containsAnyLineGroups?: readonly (readonly string[])[];
  readonly timeoutMilliseconds?: number;
}
