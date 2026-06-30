export const RuntimeMonitoringNames = {
  channel: 'monitor.profile',
  channelServerSource: 'monitor.profile.server',
  channelClientSource: 'monitor.profile.client',
  spotChannel: 'monitor.spot',
  spotNode: 'monitor.spot'
} as const;

export const PacketNames = {
  profileReq: 'ProfileReq'
} as const;

export interface ProfileReq {
  readonly value: string;
  readonly marker: string;
}

export interface ProfileRes {
  readonly value: string;
  readonly providerRid: string;
  readonly marker: string;
}

export interface EvidenceWaitReq {
  readonly containsAll: readonly string[];
  readonly containsAnyGroups: readonly (readonly string[])[];
  readonly timeoutMilliseconds?: number;
}
