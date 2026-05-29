// SPDX-License-Identifier: MPL-2.0

import { DefaultContext as Context } from '../../core/context';
import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { bindCall, closeCall, configCall } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import type {
  MemberPeerEntry,
  RegistryServiceSummaryEntry,
  RegistryServiceSummaryFilter,
  RegistryStatus,
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../../../contracts/service';
import {
  mapMemberPeerEntry,
  mapRegistryServiceSummaryEntry,
  mapRegistryStatus,
  mapRegistryTopologyEntry,
  normalizeTopologyFilter,
} from './registry_support';

export class Registry extends NativeHandle {
  private _bound = false;

  constructor(ctx: Context) {
    super(requireNative().registryNew(ctx.nativeHandle()));
  }

  /** @internal */
  nativeHandle(): unknown { return this._native; }

  bind(pubEndpoint: string, routerEndpoint: string): void {
    const normalizedPubEndpoint = validateCString(pubEndpoint, 'pubEndpoint');
    const normalizedRouterEndpoint = validateCString(routerEndpoint, 'routerEndpoint');
    bindCall('registry bind failed', () => {
      requireNative().registrySetEndpoints(this._native, normalizedPubEndpoint, normalizedRouterEndpoint);
    });
    this._bound = true;
  }

  setId(id: number): void {
    configCall('registry id set failed', () => {
      requireNative().registrySetId(this._native, id | 0);
    });
  }

  addPeer(pubEndpoint: string): void {
    const normalizedPubEndpoint = validateCString(pubEndpoint, 'pubEndpoint');
    configCall('registry peer add failed', () => {
      requireNative().registryAddPeer(this._native, normalizedPubEndpoint);
    });
  }

  setHeartbeat(intervalMs: number, timeoutMs: number): void {
    configCall('registry heartbeat configuration failed', () => {
      requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0);
    });
  }

  setBroadcastInterval(intervalMs: number): void {
    configCall('registry broadcast interval configuration failed', () => {
      requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0);
    });
  }

  setTlsServer(cert: string, key: string, requireClientCert: boolean = false): void {
    const normalizedCert = validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER);
    const normalizedKey = validateCString(key, 'key', Number.MAX_SAFE_INTEGER);
    configCall('registry TLS server configuration failed', () => {
      requireNative().registrySetTlsServer(this._native, normalizedCert, normalizedKey, requireClientCert ? 1 : 0);
    });
  }

  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('registry TLS client configuration failed', () => {
      requireNative().registrySetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }

  status(): RegistryStatus {
    return mapRegistryStatus(configCall('registry status snapshot failed', () =>
      requireNative().registryStatus(this._native)
    ));
  }

  serviceSummary(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[] {
    return (configCall('registry service summary snapshot failed', () =>
      requireNative().registryServiceSummary(this._native, filter ?? undefined) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryServiceSummaryEntry(entry as any));
  }

  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    const normalizedFilter = normalizeTopologyFilter(filter);
    return (configCall('registry topology failed', () =>
      requireNative().registryTopology(this._native, normalizedFilter) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }

  memberPeers(channelName: string): MemberPeerEntry[] {
    const normalizedChannelName = validateCString(channelName, 'channelName');
    return (configCall('registry member peer query failed', () =>
      requireNative().registryMemberPeers(this._native, normalizedChannelName) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapMemberPeerEntry(entry as any));
  }

  close(): void {
    if (this._native) {
      closeCall('registry close failed', () => {
        requireNative().registryDestroy(this._native);
      });
      this._native = null;
    }
  }
}
