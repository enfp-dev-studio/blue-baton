// SPDX-License-Identifier: GPL-3.0-only
import { useSyncExternalStore } from 'react';

/*
 * Light or dark (shadcn/ui switches on the .dark class): the appearance picked in the settings window, or the
 * system's ('' = auto). The pick is kept in local storage, so every window of the app follows it; in the app the
 * window frame follows too.
 */
export type Appearance = '' | 'light' | 'dark';

const KEY = 'bb.theme';
const media = typeof window !== 'undefined' ? window.matchMedia('(prefers-color-scheme: dark)') : null;
const listeners = new Set<() => void>();

export function chosenAppearance(): Appearance {
  try {
    const v = localStorage.getItem(KEY);
    return v === 'light' || v === 'dark' ? v : '';
  } catch {
    return '';
  }
}

function isDark() {
  const c = chosenAppearance();
  return c ? c === 'dark' : !!media?.matches;
}

function apply() {
  document.documentElement.classList.toggle('dark', isDark());
  const tauri = (window as unknown as { __TAURI__?: { window: { getCurrentWindow(): { setTheme(t: string | null): Promise<void> } } } }).__TAURI__;
  tauri?.window.getCurrentWindow().setTheme(chosenAppearance() || null).catch(() => {});
  listeners.forEach((fn) => fn());
}

export function chooseAppearance(v: Appearance) {
  try {
    if (v) localStorage.setItem(KEY, v);
    else localStorage.removeItem(KEY);
  } catch {
    /* storage off: only this window changes */
  }
  apply();
}

export function applyTheme() {
  if (!media) return;
  apply();
  media.addEventListener('change', apply);
  window.addEventListener('storage', (e) => e.key === KEY && apply());
}

export function useIsDark() {
  return useSyncExternalStore(
    (cb) => {
      listeners.add(cb);
      return () => listeners.delete(cb);
    },
    isDark,
  );
}
