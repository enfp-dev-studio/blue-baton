import { useEffect, useState } from 'react';
import { Gamepad2, Keyboard, Mouse, Smartphone } from 'lucide-react';
import { kindIcon, t } from '@/lib/board/i18n.js';
import type { Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

/* Status as a dot and a word, colored by tone */
export function StateTag({ tone, children, className }: { tone: Tone; children: React.ReactNode; className?: string }) {
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 whitespace-nowrap text-xs font-medium',
        tone === 'ok' && 'text-ok',
        tone === 'busy' && 'text-busy',
        tone === 'bad' && 'text-destructive',
        tone === 'idle' && 'text-muted-foreground',
        className,
      )}
    >
      <span
        className={cn(
          'size-[7px] shrink-0 rounded-full',
          tone === 'ok' && 'bg-ok shadow-[0_0_0_3px_color-mix(in_oklch,var(--ok)_20%,transparent)]',
          tone === 'busy' && 'animate-blink bg-busy motion-reduce:animate-none',
          tone === 'bad' && 'bg-destructive',
          tone === 'idle' && 'bg-muted-foreground/60',
        )}
      />
      {children}
    </span>
  );
}

/* Switch combos as keycaps. On a Mac the modifiers show as the symbols printed on Mac keyboards */
const IS_MAC = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || '');
const KEY_LABEL: Record<string, string> = IS_MAC
  ? { ctrl: '⌃', control: '⌃', alt: '⌥', option: '⌥', opt: '⌥', cmd: '⌘', gui: '⌘', win: '⌘', meta: '⌘', super: '⌘', shift: '⇧' }
  : { ctrl: 'Ctrl', control: 'Ctrl', alt: 'Alt', option: 'Alt', opt: 'Alt', cmd: 'Win', gui: 'Win', win: 'Win', meta: 'Win', super: 'Win', shift: 'Shift' };

export function Keycaps({ combo }: { combo?: string }) {
  if (!combo) return <span className="text-sm text-muted-foreground">{t('hosts.noCombo')}</span>;
  const keys = combo.split('+').map((k) => k.trim()).filter(Boolean);
  return (
    <span className="inline-flex flex-wrap items-center gap-1" title={combo}>
      {keys.map((k, i) => (
        <kbd
          key={i}
          className="inline-grid h-6 min-w-6 place-items-center rounded-md border border-b-2 bg-card px-1.5 font-sans text-xs font-semibold text-foreground"
        >
          {KEY_LABEL[k.toLowerCase()] || (k.length === 1 ? k.toUpperCase() : k)}
        </kbd>
      ))}
    </span>
  );
}

/* Seconds left until `until` (ms), updated once a second */
export function useSecondsLeft(until: number) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(id);
  }, []);
  return Math.max(0, Math.ceil((until - now) / 1000));
}

/*
 * Countdown ring: the stroke drains over the remaining time with a CSS animation that starts at the current
 * fraction, so only the number re-renders each second.
 */
export function CountdownRing({ until, total }: { until: number; total: number }) {
  const left = useSecondsLeft(until);
  const [start] = useState(() => {
    const l = Math.max(0, (until - Date.now()) / 1000);
    return { offset: 113.1 * (1 - l / total), dur: l };
  });
  return (
    <div className="relative size-12 shrink-0" title={t('hosts.secondsLeft', { seconds: left })}>
      <svg viewBox="0 0 46 46" className="size-12 -rotate-90" aria-hidden="true">
        <circle cx="23" cy="23" r="18" className="fill-none stroke-muted stroke-[3.5]" />
        <circle
          cx="23"
          cy="23"
          r="18"
          className="fill-none stroke-primary stroke-[3.5] [stroke-dasharray:113.1] [stroke-linecap:round] motion-reduce:[animation:none]"
          style={{ animation: `bb-drain ${start.dur}s linear forwards`, strokeDashoffset: start.offset, ['--start' as string]: start.offset }}
        />
      </svg>
      <span className="absolute inset-0 grid place-items-center text-xs font-semibold tabular-nums">{left}</span>
    </div>
  );
}

export function DeviceIcon({ kinds, appearance, className }: { kinds?: string[]; appearance?: number; className?: string }) {
  const k = kindIcon(kinds || [], appearance);
  const Icon = k === 'keyboard' ? Keyboard : k === 'mouse' ? Mouse : k === 'game' ? Gamepad2 : Smartphone;
  return <Icon className={className} strokeWidth={1.8} />;
}

/* The mark: a baton (capsule) passed between two hands (dots) */
export function Logo({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 32 32" className={className} aria-hidden="true">
      <defs>
        <linearGradient id="bb-grad" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#7c9cff" />
          <stop offset="1" stopColor="#2b59f5" />
        </linearGradient>
      </defs>
      <rect x="1" y="1" width="30" height="30" rx="9" fill="url(#bb-grad)" />
      <rect x="9.4" y="13.9" width="13.2" height="4.2" rx="2.1" fill="#fff" transform="rotate(-32 16 16)" />
      <circle cx="8.2" cy="20.9" r="1.5" fill="#fff" opacity="0.6" />
      <circle cx="23.8" cy="11.1" r="1.5" fill="#fff" />
    </svg>
  );
}
