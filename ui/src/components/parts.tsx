// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useState } from 'react';
import { Ellipsis, Gamepad2, Keyboard, Mouse, Smartphone } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { DropdownMenu, DropdownMenuContent, DropdownMenuTrigger } from '@/components/ui/dropdown-menu';
import { kindIcon, t } from '@/lib/board/i18n.js';
import type { Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

/* A host's number, or the name the user gave it instead (up to 8 characters) */
export function HostBadge({ label, on }: { label: string; on: boolean }) {
  return (
    <span
      className={cn(
        'inline-flex h-5 min-w-5 shrink-0 items-center justify-center whitespace-nowrap rounded-sm px-1 text-2xs font-semibold',
        on ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
      )}
    >
      {label}
    </span>
  );
}

/* Status as a dot and a word, colored by tone */
export function StateTag({ tone, children, className }: { tone: Tone; children: React.ReactNode; className?: string }) {
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 whitespace-nowrap text-xs',
        tone === 'ok' && 'text-ok',
        tone === 'busy' && 'text-busy',
        tone === 'bad' && 'text-destructive',
        tone === 'idle' && 'text-muted-foreground',
        className,
      )}
    >
      <span
        className={cn(
          'size-1.5 shrink-0 rounded-full',
          tone === 'ok' && 'bg-ok',
          tone === 'busy' && 'animate-blink bg-busy motion-reduce:animate-none',
          tone === 'bad' && 'bg-destructive',
          tone === 'idle' && 'bg-muted-foreground/60',
        )}
      />
      {children}
    </span>
  );
}

/*
 * A titled group of rows with hairline separators, as in macOS System Settings. `aside` sits at the right of the
 * title; `footer` under the group.
 */
export function Group({
  title,
  aside,
  footer,
  children,
  className,
}: {
  title?: React.ReactNode;
  aside?: React.ReactNode;
  footer?: React.ReactNode;
  children: React.ReactNode;
  className?: string;
}) {
  return (
    <section className="grid gap-1.5">
      {(title || aside) && (
        <div className="flex flex-wrap items-baseline justify-between gap-x-3 px-1">
          {title && <h2 className="text-ui font-semibold">{title}</h2>}
          {aside && <span className="text-xs text-muted-foreground">{aside}</span>}
        </div>
      )}
      <div className={cn('overflow-hidden rounded-lg border bg-card', className)}>{children}</div>
      {footer && <div className="px-1 text-xs text-muted-foreground">{footer}</div>}
    </section>
  );
}

/* The "more" button at the end of a row; its menu holds the row's less frequent actions */
export function RowMenu({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <Button size="icon-sm" variant="ghost" className="size-7 text-muted-foreground" aria-label={label} title={label}>
          <Ellipsis />
        </Button>
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end" className="min-w-40 text-ui">
        {children}
      </DropdownMenuContent>
    </DropdownMenu>
  );
}

/* Switch combos the way menus show shortcuts. On a Mac the modifiers show as the symbols printed on Mac keyboards */
const IS_MAC = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || '');
const KEY_LABEL: Record<string, string> = IS_MAC
  ? { ctrl: '⌃', control: '⌃', alt: '⌥', option: '⌥', opt: '⌥', cmd: '⌘', gui: '⌘', win: '⌘', meta: '⌘', super: '⌘', shift: '⇧' }
  : { ctrl: 'Ctrl', control: 'Ctrl', alt: 'Alt', option: 'Alt', opt: 'Alt', cmd: 'Win', gui: 'Win', win: 'Win', meta: 'Win', super: 'Win', shift: 'Shift' };

const KEY_NAME: Record<string, string> = {
  left: '←', right: '→', up: '↑', down: '↓', enter: '↩', return: '↩', esc: 'Esc', escape: 'Esc', tab: '⇥',
  backspace: '⌫', delete: '⌦', space: 'Space', minus: '-', equal: '=', grave: '`', backquote: '`',
  pageup: 'PgUp', pagedown: 'PgDn', home: 'Home', end: 'End', insert: 'Ins', capslock: '⇪',
  printscreen: 'PrtSc', scrolllock: 'ScrLk', pause: 'Pause',
};

/* "ctrl+alt+1" → "⌃⌥1" on a Mac, "Ctrl+Alt+1" elsewhere */
export function comboLabel(keys: string[]) {
  const label = keys.map((k) => {
    const l = k.toLowerCase();
    return KEY_LABEL[l] || KEY_NAME[l] || (/^f\d+$/.test(l) ? l.toUpperCase() : k.length === 1 ? k.toUpperCase() : k);
  });
  return IS_MAC ? label.join('') : label.join('+');
}

export function Keycaps({ combo, className }: { combo?: string; className?: string }) {
  if (!combo) return <span className={cn('text-xs text-muted-foreground', className)}>{t('hosts.noCombo')}</span>;
  const keys = combo.split('+').map((k) => k.trim()).filter(Boolean);
  return (
    <kbd className={cn('whitespace-nowrap font-sans text-xs tracking-[0.12em] text-muted-foreground', className)} title={combo}>
      {comboLabel(keys)}
    </kbd>
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

/*
 * The app icon at UI size (app/assets/make_icons.py draws the full one, same geometry on 1024): a hollow baton with a
 * band. Tile in the text colour, baton in the background colour and band in the accent, so it follows the theme
 */
export function Logo({ className }: { className?: string }) {
  return (
    <svg viewBox="100 100 824 824" className={className} aria-hidden="true">
      <rect x="100" y="100" width="824" height="824" rx="185" className="fill-foreground" />
      <g transform="rotate(-35 512 512)" className="fill-background">
        <path d="M219 392 H805 V632 H219 A57 120 0 0 1 219 392 Z" />
        <path d="M370 392 A57 120 0 0 0 370 632 H450 A57 120 0 0 1 450 392 Z" className="fill-primary" />
        <ellipse cx="805" cy="512" rx="57" ry="120" />
        <ellipse cx="805" cy="512" rx="32" ry="78" className="fill-foreground" />
      </g>
    </svg>
  );
}
