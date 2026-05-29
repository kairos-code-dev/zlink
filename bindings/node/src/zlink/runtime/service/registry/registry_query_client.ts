// SPDX-License-Identifier: MPL-2.0

import { RuntimeContext as Context } from '../../core/context';
import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { closeCall, configCall, connectCall } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import type {
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../../../contracts/service';
import {
  mapRegistryTopologyEntry,
  normalizeTopologyFilter,
  type RegistryTopologyEntryRaw,
} from './registry_support';

export class RegistryQueryClient extends NativeHandle {
  constructor(ctx: Context) {
    super(requireNative().registryQueryClientNew(ctx.nativeHandle()));
  }

  connect(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('registry query client connect failed', () => {
      requireNative().registryQueryClientConnect(this._native, normalizedEndpoint);
    });
  }

  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    const normalizedFilter = normalizeTopologyFilter(filter);
    return (configCall('registry query topology failed', () =>
      requireNative().registryQueryTopology(this._native, normalizedFilter) as RegistryTopologyEntryRaw[]
    ))
      .map(mapRegistryTopologyEntry);
  }

  close(): void {
    if (this._native) {
      closeCall('registry query client close failed', () => {
        requireNative().registryQueryDestroy(this._native);
      });
      this._native = null;
    }
  }
}
