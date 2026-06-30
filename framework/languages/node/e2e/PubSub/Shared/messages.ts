export const PubSubNames = {
  channel: 'events',
  mainTopic: 'orders',
  otherTopic: 'billing'
} as const;

export const PacketNames = {
  eventNotify: 'EventNotify',
  missingEventNotify: 'MissingEventNotify'
} as const;

export interface EventNotify {
  readonly runId: string;
  readonly sequence: number;
  readonly value: string;
}

export interface MissingEventNotify {
  readonly runId: string;
  readonly sequence: number;
  readonly value: string;
}

export interface EvidenceWaitRequest {
  readonly containsAll?: readonly string[];
  readonly containsAnyGroups?: readonly (readonly string[])[];
  readonly containsAllLineGroups?: readonly (readonly string[])[];
  readonly containsAnyLineGroups?: readonly (readonly string[])[];
  readonly timeoutMilliseconds?: number;
}
