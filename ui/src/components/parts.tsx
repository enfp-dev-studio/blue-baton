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
          {title && <h2 className="text-[13px] font-semibold">{title}</h2>}
          {aside && <span className="text-xs text-muted-foreground">{aside}</span>}
        </div>
      )}
      <div className={cn('overflow-hidden rounded-[10px] border bg-card', className)}>{children}</div>
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
      <DropdownMenuContent align="end" className="min-w-40 text-[13px]">
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

export function Keycaps({ combo, className }: { combo?: string; className?: string }) {
  if (!combo) return <span className={cn('text-xs text-muted-foreground', className)}>{t('hosts.noCombo')}</span>;
  const keys = combo.split('+').map((k) => k.trim()).filter(Boolean);
  const label = keys.map((k) => KEY_LABEL[k.toLowerCase()] || (k.length === 1 ? k.toUpperCase() : k));
  return (
    <kbd className={cn('whitespace-nowrap font-sans text-xs tracking-[0.12em] text-muted-foreground', className)} title={combo}>
      {IS_MAC ? label.join('') : label.join('+')}
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

/* The mark: a baton (capsule) passed between two hands (dots) */
/* The app icon at UI size (app/assets/make_icons.py draws the full one): a hollow baton passed at speed */
export function Logo({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 32 32" className={className} aria-hidden="true">
      <defs>
        <linearGradient id="bb-bg" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#2c3d8a" />
          <stop offset="1" stopColor="#0f1538" />
        </linearGradient>
        <linearGradient id="bb-tube" x1="0" y1="-2.2" x2="0" y2="2.2" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#c9d6ff" />
          <stop offset="0.5" stopColor="#3d6bff" />
          <stop offset="1" stopColor="#1a32a8" />
        </linearGradient>
      </defs>
      <rect x="1" y="1" width="30" height="30" rx="9" fill="url(#bb-bg)" />
      <g transform="translate(17.6 15) rotate(-33)">
        <rect x="-12.6" y="-1.9" width="4.2" height="1.2" rx="0.6" fill="#9db3ff" opacity="0.7" />
        <rect x="-11.4" y="0.8" width="3" height="1.2" rx="0.6" fill="#9db3ff" opacity="0.5" />
        <ellipse cx="-7.2" cy="0" rx="0.8" ry="2.2" fill="url(#bb-tube)" />
        <rect x="-7.2" y="-2.2" width="14.4" height="4.4" fill="url(#bb-tube)" />
        <ellipse cx="7.2" cy="0" rx="0.8" ry="2.2" fill="#dfe6ff" />
        <ellipse cx="7.3" cy="0" rx="0.5" ry="1.5" fill="#16246e" />
      </g>
    </svg>
  );
}
