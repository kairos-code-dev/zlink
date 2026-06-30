export const ChannelNames = {
  profile: 'profile'
} as const;

export const PacketNames = {
  profileRequest: 'ProfileRequest'
} as const;

export interface ProfileRequest {
  readonly value: string;
  readonly marker?: string;
}

export interface ProfileReply {
  readonly value: string;
  readonly providerRid: string;
  readonly marker?: string;
}

export interface EvidenceWaitRequest {
  readonly contains: string;
  readonly timeoutMilliseconds?: number;
}
