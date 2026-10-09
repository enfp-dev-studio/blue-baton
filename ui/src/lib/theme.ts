import { useSyncExternalStore } from 'react';

/* Follows the system light/dark setting (shadcn/ui switches on the .dark class) */
const media = typeof window !== 'undefined' ? window.matchMedia('(prefers-color-scheme: dark)') : null;

export function applyTheme() {
  if (!media) return;
  const set = () => document.documentElement.classList.toggle('dark', media.matches);
  set();
  media.addEventListener('change', set);
}

export function useIsDark() {
  return useSyncExternalStore(
    (cb) => {
      media?.addEventListener('change', cb);
      return () => media?.removeEventListener('change', cb);
    },
    () => media?.matches ?? false,
  );
}
