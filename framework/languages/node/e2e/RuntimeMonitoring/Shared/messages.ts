export const RuntimeMonitoringNames = {
  channel: 'monitor.profile',
  channelServerSource: 'monitor.profile.server',
  channelClientSource: 'monitor.profile.client',
  locationRuntimeSource: 'monitor.location-runtime',
  spotChannel: 'monitor.spot',
  spotNode: 'monitor.spot'
} as const;

export const PacketNames = {
  profileReq: 'ProfileReq'
} as const;

export class ProfileReq {
  constructor(
    readonly value: string,
    readonly marker: string
  ) {}
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
