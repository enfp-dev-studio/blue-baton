// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useRef, useState } from 'react';
import { REGEXP_ONLY_DIGITS } from 'input-otp';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { InputOTP, InputOTPGroup, InputOTPSlot } from '@/components/ui/input-otp';
import { CountdownRing, DeviceIcon, StateTag, useSecondsLeft } from '@/components/parts';
import { kindText, t, tParts } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Found } from '@/lib/board/types';
import { cn } from '@/lib/utils';

const HOST_WINDOW_S = 180;
const SCAN_S = 60;

/*
 * Focused as soon as it appears, so the code can be typed right away; sends itself at the sixth digit. The code is
 * read off another screen, so coming back to this window must not need a click on the field: the field takes the
 * focus back when the window does, digits typed anywhere in the window go into it, and pasting anywhere takes the
 * first six digits of the clipboard and sends them.
 */
function CodeInput({ value, onChange, onComplete, label }: { value: string; onChange: (v: string) => void; onComplete: (v: string) => void; label: string }) {
  const input = useRef<HTMLInputElement>(null);
  const current = useRef(value);
  current.current = value;
  useEffect(() => {
    const own = (t: EventTarget | null) => t === input.current;
    const focus = () => setTimeout(() => input.current?.focus(), 0);
    const set = (v: string) => {
      current.current = v; /* before React re-renders, so fast typing does not drop a digit */
      onChange(v);
      if (v.length === 6) onComplete(v);
      focus();
    };
    const key = (e: KeyboardEvent) => {
      if (own(e.target) || e.metaKey || e.ctrlKey || e.altKey) return;
      if (/^[0-9]$/.test(e.key) && current.current.length < 6) {
        e.preventDefault();
        set(current.current + e.key);
      } else if (e.key === 'Backspace' && current.current) {
        e.preventDefault();
        set(current.current.slice(0, -1));
      }
    };
    const paste = (e: ClipboardEvent) => {
      if (own(e.target)) return;
      const digits = (e.clipboardData?.getData('text') || '').replace(/\D/g, '');
      if (digits.length < 6) return;
      e.preventDefault();
      set(digits.slice(0, 6));
    };
    const click = (e: MouseEvent) => {
      const el = e.target as HTMLElement | null;
      if (!el?.closest('button, a, input, textarea, select, [role="menuitem"]')) focus();
    };
    window.addEventListener('focus', focus);
    document.addEventListener('keydown', key, true);
    document.addEventListener('paste', paste, true);
    document.addEventListener('mouseup', click, true);
    return () => {
      window.removeEventListener('focus', focus);
      document.removeEventListener('keydown', key, true);
      document.removeEventListener('paste', paste, true);
      document.removeEventListener('mouseup', click, true);
    };
  }, [onChange, onComplete]);
  return (
    <InputOTP
      ref={input}
      autoFocus
      maxLength={6}
      pattern={REGEXP_ONLY_DIGITS}
      value={value}
      onChange={onChange}
      onComplete={onComplete}
      aria-label={label}
      containerClassName="w-full max-w-72"
    >
      {/* Six equal slots across the width, so a narrow window still fits them */}
      <InputOTPGroup className="w-full">
        {[0, 1, 2, 3, 4, 5].map((i) => (
          <InputOTPSlot key={i} index={i} className="h-11 min-w-0 flex-1 text-lg" />
        ))}
      </InputOTPGroup>
    </InputOTP>
  );
}

/* ---- Add a host: the pairing window (up to 180 s), then the passkey shown on the computer ---- */

export function HostPairingDialog({ s, a }: { s: BoardState; a: Actions }) {
  const [code, setCode] = useState('');
  const p = s.hostPrompt;
  const c = s.hostCandidate;
  useEffect(() => setCode(''), [p?.addr]);
  return (
    <Dialog open={s.hostWindow.open} onOpenChange={(open) => !open && a.closeHostWindow()}>
      <DialogContent aria-describedby={undefined} className="max-h-[calc(100dvh-2rem)] overflow-y-auto overflow-x-hidden sm:max-w-lg">
        <DialogHeader className="flex-row items-center gap-4 space-y-0 text-left">
          {s.hostWindow.open && <CountdownRing until={s.hostWindow.until} total={HOST_WINDOW_S} />}
          <div className="grid gap-1">
            <DialogTitle>{t('hosts.windowTitle')}</DialogTitle>
          </div>
        </DialogHeader>
        <p className="text-sm text-muted-foreground">{t('hosts.step')}</p>
        {c && (
          <StateTag tone="busy" className="text-sm">
            {c.state === 'securing' ? t('hosts.verifying') : t('hosts.candidate')}
          </StateTag>
        )}
        {p && (
          <form
            className="grid gap-3 rounded-xl border border-primary p-4 shadow-[0_0_0_4px_color-mix(in_oklch,var(--primary)_14%,transparent)]"
            onSubmit={(e) => {
              e.preventDefault();
              if (code.length === 6) a.hostPasskey(code);
            }}
          >
            <p className="text-sm font-medium">{t('hosts.codeLabel')}</p>
            {p.len > 0 ? (
              /* Typed on the board's keyboard: the digits as typed (only how many from older firmware) */
              <div className="grid w-full max-w-72 grid-cols-6 gap-1.5" aria-label={t('hosts.passkeyDigits', { n: p.len })}>
                {[0, 1, 2, 3, 4, 5].map((i) => (
                  <span
                    key={i}
                    className={cn('grid h-11 place-items-center rounded-md border', i < p.len ? 'border-primary bg-primary/10' : 'bg-muted')}
                  >
                    {i < p.len && (p.digits ? <span className="text-lg tabular-nums">{p.digits[i]}</span> : <span className="size-2 rounded-full bg-primary" />)}
                  </span>
                ))}
              </div>
            ) : (
              <CodeInput value={code} onChange={setCode} onComplete={(v) => a.hostPasskey(v)} label={t('hosts.passkeyLabel')} />
            )}
            <p className="text-xs text-muted-foreground">{t('hosts.codeBoard')}</p>
          </form>
        )}
        <DialogFooter>
          <Button variant="ghost" onClick={a.closeHostWindow}>
            {t('common.close')}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

/* ---- Add an input device: scan, pick, answer the pairing prompt ---- */

function Prompt({ s, a }: { s: BoardState; a: Actions }) {
  const [code, setCode] = useState('');
  const p = s.prompt;
  if (!p) return null;
  const box = 'grid gap-3 rounded-xl border border-primary p-4 shadow-[0_0_0_4px_color-mix(in_oklch,var(--primary)_14%,transparent)]';
  if (p.type === 'numcmp') {
    return (
      <div className={box}>
        <p className="text-sm">{t('prompt.numcmp')}</p>
        <div className="font-mono text-4xl font-semibold tracking-[0.22em]">{p.code}</div>
        <div className="flex gap-2">
          <Button onClick={() => a.accept(true)}>{t('prompt.match')}</Button>
          <Button variant="outline" onClick={() => a.accept(false)}>{t('prompt.noMatch')}</Button>
        </div>
      </div>
    );
  }
  if (p.type === 'passkey_request') {
    return (
      <form className={box} onSubmit={(e) => { e.preventDefault(); if (code.length === 6) a.passkey(code); }}>
        <p className="text-sm">{t('prompt.passkeyRequest')}</p>
        <div className="flex flex-wrap items-center gap-2">
          <CodeInput value={code} onChange={setCode} onComplete={(v) => a.passkey(v)} label={t('prompt.passkeyLabel')} />
          <Button type="submit" disabled={code.length !== 6}>{t('common.send')}</Button>
        </div>
      </form>
    );
  }
  return (
    <div className={box}>
      <p className="text-sm">{t('prompt.passkeyDisplay')}</p>
      <div className="font-mono text-4xl font-semibold tracking-[0.22em]">{p.code}</div>
    </div>
  );
}

function FoundRow({ f, a }: { f: Found; a: Actions }) {
  const busy = f.state === 'connecting';
  return (
    <li
      title={`${f.addr} · ${t('add.signal', { rssi: f.rssi })}`}
      className="grid grid-cols-[32px_minmax(0,1fr)_auto] items-center gap-3 rounded-lg px-2 py-1.5 hover:bg-muted/60"
    >
      <span className="grid size-8 place-items-center rounded-lg bg-muted text-muted-foreground">
        <DeviceIcon appearance={f.appearance} className="size-4" />
      </span>
      <div className="min-w-0 leading-tight">
        <div className="flex items-center gap-2">
          <span className="truncate font-medium">{f.name || t('common.unnamedDevice')}</span>
          {f.paired && <span className="shrink-0 rounded-full bg-muted px-2 py-0.5 text-2xs font-semibold text-muted-foreground">{t('add.paired')}</span>}
        </div>
        <div className="text-xs text-muted-foreground">{kindText([], f.appearance)}</div>
      </div>
      {f.hid === false ? (
        <span className="text-sm text-muted-foreground">{t('add.notInput')}</span>
      ) : (
        <Button size="sm" disabled={busy} onClick={() => a.connectDevice(f.addr)}>
          {busy ? t('add.connecting') : t('add.connect')}
        </Button>
      )}
    </li>
  );
}

function ScanButton({ s, a }: { s: BoardState; a: Actions }) {
  const left = useSecondsLeft(s.scanUntil);
  if (s.scanning) {
    return (
      <Button variant="outline" onClick={() => a.scan(false)}>
        {tParts('add.scanning', { seconds: <span key="s" className="tabular-nums">{left}</span> })}
      </Button>
    );
  }
  return (
    <Button onClick={() => a.scan(true)} disabled={s.scanPending}>
      {s.scanPending ? t('add.starting') : t('add.start')}
    </Button>
  );
}

export function AddDeviceDialog({ s, a }: { s: BoardState; a: Actions }) {
  const found = [...s.found.values()].filter((f) => s.showAll || f.hid !== false).sort((x, y) => y.rssi - x.rssi);
  return (
    <Dialog open={s.addOpen} onOpenChange={(open) => !open && a.closeAdd()}>
      <DialogContent aria-describedby={undefined} className="flex max-h-[calc(100dvh-2rem)] flex-col overflow-hidden sm:max-w-xl">
        <DialogHeader className="flex-row items-center gap-4 space-y-0 text-left">
          {s.scanning && <CountdownRing key={s.scanUntil} until={s.scanUntil} total={SCAN_S} />}
          <div className="grid gap-1">
            <DialogTitle>{t('add.title')}</DialogTitle>
          </div>
        </DialogHeader>
        <p className="text-sm text-muted-foreground">{t('add.step1')}</p>
        <Prompt s={s} a={a} />
        <div className="flex flex-wrap items-center gap-4">
          <ScanButton s={s} a={a} />
        </div>
        {/* The list takes the rest of the dialog's height and scrolls on its own */}
        <div className="-mx-2 min-h-0 flex-1 overflow-y-auto">
          {found.length ? (
            <ul className="grid gap-1">
              {found.map((f) => <FoundRow key={f.addr} f={f} a={a} />)}
            </ul>
          ) : (
            s.scanning && <p className="py-3 text-sm text-muted-foreground">{t('add.searching')}</p>
          )}
        </div>
      </DialogContent>
    </Dialog>
  );
}

/* ---- Confirm (remove) ---- */

export interface ConfirmRequest {
  title: string;
  body: string;
  action: () => void;
  /* The button: Remove (destructive) unless given */
  confirmText?: string;
  destructive?: boolean;
}

export function ConfirmDialog({ req, onClose }: { req: ConfirmRequest | null; onClose: () => void }) {
  return (
    <Dialog open={!!req} onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-h-[calc(100dvh-2rem)] overflow-y-auto overflow-x-hidden sm:max-w-md">
        <DialogHeader>
          <DialogTitle>{req?.title}</DialogTitle>
          <DialogDescription className="whitespace-pre-line">{req?.body}</DialogDescription>
        </DialogHeader>
        <DialogFooter>
          <Button variant="ghost" onClick={onClose}>{t('common.cancel')}</Button>
          <Button
            variant={req?.destructive === false ? 'default' : 'destructive'}
            onClick={() => {
              req?.action();
              onClose();
            }}
          >
            {req?.confirmText || t('common.remove')}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
