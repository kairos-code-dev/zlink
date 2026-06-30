export const RuntimeMonitoringNames = {
  channel: 'monitor.profile',
  channelServerSource: 'monitor.profile.server',
  channelClientSource: 'monitor.profile.client',
  spotChannel: 'monitor.spot',
  spotNode: 'monitor.spot'
} as const;

export const PacketNames = {
  profileRequest: 'ProfileRequest'
} as const;

export interface ProfileRequest {
  readonly value: string;
  readonly marker: string;
}

export interface ProfileReply {
  readonly value: string;
  readonly providerRid: string;
  readonly marker: string;
}

export interface EvidenceWaitRequest {
  readonly containsAll: readonly string[];
  readonly containsAnyGroups: readonly (readonly string[])[];
  readonly timeoutMilliseconds?: number;
}
