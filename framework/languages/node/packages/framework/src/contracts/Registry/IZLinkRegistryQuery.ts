import type {
  ZLinkMemberPeerEntry,
  ZLinkRegistryServiceSummaryEntry,
  ZLinkRegistryServiceSummaryFilter,
  ZLinkRegistryStatus,
  ZLinkRegistryTopologyEntry,
  ZLinkRegistryTopologyFilter
} from './Models';

export interface ZLinkRegistryQuery {
  status(signal?: AbortSignal): Promise<ZLinkRegistryStatus>;
  serviceSummary(
    filter?: ZLinkRegistryServiceSummaryFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryServiceSummaryEntry[]>;
  topology(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]>;
  memberPeers(channelName: string, signal?: AbortSignal): Promise<readonly ZLinkMemberPeerEntry[]>;
}

export interface ZLinkRegistryQueryClient {
  topology(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]>;
}

export interface ZLinkRegistryQueryClientOptions {
  endpoint: string;
}
