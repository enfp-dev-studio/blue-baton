import { useEffect, useState } from 'react';
import { REGEXP_ONLY_DIGITS } from 'input-otp';
import { Button } from '@/components/ui/button';
import { Checkbox } from '@/components/ui/checkbox';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { InputOTP, InputOTPGroup, InputOTPSlot } from '@/components/ui/input-otp';
import { Label } from '@/components/ui/label';
import { CountdownRing, DeviceIcon, StateTag, useSecondsLeft } from '@/components/parts';
import { kindText, t, tParts } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Found } from '@/lib/board/types';
import { cn } from '@/lib/utils';

const HOST_WINDOW_S = 180;
const SCAN_S = 60;

function Steps({ items }: { items: string[] }) {
  return (
    <ol className="grid gap-2">
      {items.map((text, i) => (
        <li key={i} className="flex items-baseline gap-2.5 text-sm text-muted-foreground">
          <span className="grid size-5 shrink-0 translate-y-[-1px] place-items-center rounded-full bg-muted text-[11px] font-bold">{i + 1}</span>
          {text}
        </li>
      ))}
    </ol>
  );
}

function CodeInput({ value, onChange, label }: { value: string; onChange: (v: string) => void; label: string }) {
  return (
    <InputOTP maxLength={6} pattern={REGEXP_ONLY_DIGITS} value={value} onChange={onChange} aria-label={label}>
      <InputOTPGroup>
        {[0, 1, 2, 3, 4, 5].map((i) => (
          <InputOTPSlot key={i} index={i} className="size-11 text-lg" />
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
      <DialogContent className="max-h-[calc(100dvh-2rem)] overflow-y-auto overflow-x-hidden sm:max-w-lg">
        <DialogHeader className="flex-row items-center gap-4 space-y-0 text-left">
          {s.hostWindow.open && <CountdownRing until={s.hostWindow.until} total={HOST_WINDOW_S} />}
          <div className="grid gap-1">
            <DialogTitle>{t('hosts.windowTitle')}</DialogTitle>
            <DialogDescription>{t('hosts.windowHint')}</DialogDescription>
          </div>
        </DialogHeader>
        <Steps items={[t('hosts.stepMac'), t('hosts.stepWindows'), t('hosts.stepCode')]} />
        {c && (
          <StateTag tone="busy" className="text-sm">
            {c.state === 'securing' ? t('hosts.verifying') : t('hosts.candidate', { addr: c.addr })}
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
            <p className="text-sm">{t('hosts.passkeyHelp')}</p>
            <div className="flex gap-2" aria-label={t('hosts.passkeyDigits', { n: p.len })}>
              {[0, 1, 2, 3, 4, 5].map((i) => (
                <span
                  key={i}
                  className={cn('grid h-11 w-9 place-items-center rounded-lg border-[1.5px]', i < p.len ? 'border-primary bg-primary/10' : 'bg-muted')}
                >
                  {i < p.len && <span className="size-2 rounded-full bg-primary" />}
                </span>
              ))}
            </div>
            <p className="text-sm text-muted-foreground">{t('hosts.passkeyFallback')}</p>
            <div className="flex flex-wrap items-center gap-2">
              <CodeInput value={code} onChange={setCode} label={t('hosts.passkeyLabel')} />
              <Button type="submit" disabled={code.length !== 6}>
                {t('common.send')}
              </Button>
            </div>
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
          <CodeInput value={code} onChange={setCode} label={t('prompt.passkeyLabel')} />
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
    <li className="grid grid-cols-[40px_minmax(0,1fr)_auto] items-center gap-3 rounded-xl p-2 hover:bg-muted/60">
      <span className="grid size-10 place-items-center rounded-xl bg-muted text-muted-foreground">
        <DeviceIcon appearance={f.appearance} className="size-5" />
      </span>
      <div className="min-w-0">
        <div className="flex items-center gap-2">
          <span className="truncate font-semibold">{f.name || t('common.unnamedDevice')}</span>
          {f.paired && <span className="rounded-full bg-muted px-2 py-0.5 text-[11px] font-semibold text-muted-foreground">{t('add.paired')}</span>}
        </div>
        <div className="flex flex-wrap gap-3 text-sm text-muted-foreground">
          <span>{kindText([], f.appearance)}</span>
          <span>{t('add.signal', { rssi: f.rssi })}</span>
          <span className="font-mono text-xs text-muted-foreground/70">{f.addr}</span>
        </div>
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
      <DialogContent className="max-h-[calc(100dvh-2rem)] overflow-y-auto overflow-x-hidden sm:max-w-xl">
        <DialogHeader className="flex-row items-center gap-4 space-y-0 text-left">
          {s.scanning && <CountdownRing key={s.scanUntil} until={s.scanUntil} total={SCAN_S} />}
          <div className="grid gap-1">
            <DialogTitle>{t('add.title')}</DialogTitle>
            <DialogDescription>{t('add.hint')}</DialogDescription>
          </div>
        </DialogHeader>
        <Steps items={[t('add.step1'), t('add.step2')]} />
        <Prompt s={s} a={a} />
        <div className="flex flex-wrap items-center gap-4">
          <ScanButton s={s} a={a} />
          <div className="flex items-center gap-2">
            <Checkbox id="show-all" checked={s.showAll} onCheckedChange={(v) => a.showAll(v === true)} />
            <Label htmlFor="show-all" className="text-sm font-normal text-muted-foreground">{t('add.showAll')}</Label>
          </div>
        </div>
        <div className="max-h-[45vh] overflow-y-auto">
          {found.length ? (
            <ul className="grid gap-1">
              {found.map((f) => <FoundRow key={f.addr} f={f} a={a} />)}
            </ul>
          ) : (
            <p className="py-3 text-sm text-muted-foreground">{s.scanning ? t('add.searching') : t('add.idleHint')}</p>
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
            variant="destructive"
           
            onClick={() => {
              req?.action();
              onClose();
            }}
          >
            {t('common.remove')}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
