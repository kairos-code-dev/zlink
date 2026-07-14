import path from 'node:path';
import { createRequire } from 'node:module';
import type { EvidenceStore as EvidenceStoreType } from '../evidence-store';

const requireEvidenceStore = createRequire(__filename);
const evidenceStoreModule = requireEvidenceStore(
  path.resolve(process.env.ZLINK_NODE_E2E_ROOT ?? path.resolve(process.cwd(), 'e2e'), 'evidence-store.js')
) as { EvidenceStore: typeof EvidenceStoreType };

export const EvidenceStore = evidenceStoreModule.EvidenceStore;
export type EvidenceStore = EvidenceStoreType;
