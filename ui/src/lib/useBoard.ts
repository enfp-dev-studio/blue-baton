// SPDX-License-Identifier: GPL-3.0-only
import { useSyncExternalStore } from 'react';
import type { BoardState, Controller } from './board/types';

/* Re-render on every store change. The store updates its state object in place and bumps a version */
export function useBoard(ctl: Controller): BoardState {
  useSyncExternalStore(
    (cb) => ctl.store.subscribe(() => cb()),
    () => ctl.store.version,
  );
  return ctl.store.state;
}
