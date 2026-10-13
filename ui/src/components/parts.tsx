// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useState } from 'react';
import { Ellipsis, Gamepad2, Keyboard, Mouse, Smartphone } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { DropdownMenu, DropdownMenuContent, DropdownMenuTrigger } from '@/components/ui/dropdown-menu';
import { kindIcon, t } from '@/lib/board/i18n.js';
import type { Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

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
          'size-2 shrink-0',
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
 * A titled box of rows with ink lines between them. `n` numbers the box in the title ("01 / Hosts"); `aside` sits at
 * the right of the title; `footer` under the box.
 */
export function Group({
  n,
  title,
  aside,
  footer,
  children,
  className,
}: {
  n?: number;
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
          {title && (
            <h2 className="text-2xs font-bold tracking-[0.08em] uppercase">
              {n !== undefined && <span className="text-muted-foreground">{String(n).padStart(2, '0')} / </span>}
              {title}
            </h2>
          )}
          {aside && <span className="text-2xs text-muted-foreground uppercase">{aside}</span>}
        </div>
      )}
      <div className={cn('overflow-hidden border-[1.5px] border-foreground bg-card', className)}>{children}</div>
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
    <kbd className={cn('whitespace-nowrap border border-foreground bg-card px-1.5 py-0.5 font-mono text-2xs font-bold tracking-[0.12em] text-card-foreground', className)} title={combo}>
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
 * The app icon at UI size (app/assets/make_icons.py draws the full one, same geometry on 1024): a hollow baton passed
 * at speed. Ink tile and paper baton, so it follows the theme
 */
export function Logo({ className }: { className?: string }) {
  return (
    <svg viewBox="100 100 824 824" className={className} aria-hidden="true">
      <rect x="100" y="100" width="824" height="824" rx="185" className="fill-foreground" />
      <g transform="rotate(-35 512 512)" className="fill-background">
        <rect x="186" y="437" width="112" height="62" rx="31" />
        <rect x="156" y="525" width="142" height="62" rx="31" />
        <path d="M392 428 H800 V596 H392 A40 84 0 0 1 392 428 Z" />
        <ellipse cx="800" cy="512" rx="40" ry="84" />
        <ellipse cx="800" cy="512" rx="22" ry="54" className="fill-foreground" />
      </g>
    </svg>
  );
}
