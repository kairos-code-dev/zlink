export const ChannelNames = {
  profile: 'profile'
} as const;

export const PacketNames = {
  profileReq: 'ProfileReq'
} as const;

export interface ProfileReq {
  readonly value: string;
  readonly marker?: string;
}

export interface ProfileRes {
  readonly value: string;
  readonly providerRid: string;
  readonly marker?: string;
}

export interface EvidenceWaitReq {
  readonly contains: string;
  readonly timeoutMilliseconds?: number;
}
