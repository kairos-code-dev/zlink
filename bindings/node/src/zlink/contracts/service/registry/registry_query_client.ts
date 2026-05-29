// SPDX-License-Identifier: MPL-2.0

import type {
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../index';

export interface RegistryQueryClient {
  connect(endpoint: string): void;
  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  close(): void;
}
