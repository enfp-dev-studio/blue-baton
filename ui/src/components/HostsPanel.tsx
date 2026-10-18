// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useRef, useState } from 'react';
import { toast } from 'sonner';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { DropdownMenuItem, DropdownMenuSeparator } from '@/components/ui/dropdown-menu';
import { Input } from '@/components/ui/input';
import { RadioGroup as RadioGroupPrimitive } from 'radix-ui';
import { Group, HostBadge, Keycaps, RowMenu, StateTag, comboLabel } from '@/components/parts';
import { DEFAULT_COMBOS, comboFromEvent, modifiers, sameCombo } from '@/lib/board/combo.js';
import { hostLabel } from '@/lib/board/controller.js';
import { hostStateText, hostStateTone, t, tParts } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Host, Tone } from '@/lib/board/types';
import { cn } from '@/lib/utils';

type Confirm = (opts: { title: string; body: string; action: () => void }) => void;

const ROW = 52;

/*
 * The three host slots as rows, with the baton: a short bar at the left edge of the host that receives the input.
 * It slides to the new host on a switch (a transition on --to). Off with reduced motion.
 */
export function HostsPanel({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  const active = s.hosts.get(s.activeHost);
  const [renaming, setRenaming] = useState<Renaming | null>(null);
  const rename = (n: number) => setRenaming({ n, nick: s.hosts.get(n)?.nick || '' });
  /* The one combo being recorded (0: none). One for the whole list: WebKit does not focus a clicked button, so a
     recorder that ends on blur would stay on when another one is clicked */
  const [recording, setRecording] = useState(0);
  return (
    <>
    <Group
      title={t('hosts.title')}
      aside={
        active
          ? tParts('hosts.sendingTo', { name: <b key="n" className="font-medium text-foreground">{active.name || t('hosts.unnamed')}</b> })
          : s.hosts.size ? undefined : t('hosts.noneActive')
      }
      footer={
        !s.combosDefault && (
          <Button variant="link" className="h-auto p-0 text-xs" onClick={() => a.setCombos(DEFAULT_COMBOS)}>
            {t('combo.reset')}
          </Button>
        )
      }
    >
      <RadioGroupPrimitive.Root
        asChild
        value={String(s.activeHost)}
        onValueChange={(v) => a.selectHost(Number(v))}
        aria-label={t('hosts.groupLabel')}
      >
        <ul className="relative list-none divide-y p-0" style={{ ['--to' as string]: s.activeHost - 1 }}>
          {s.hosts.has(s.activeHost) && (
            <li
              aria-hidden="true"
              className="pointer-events-none absolute left-0 top-0 z-10 flex h-[52px] items-center translate-y-[calc(var(--to)*52px)] transition-[translate] duration-500 ease-[cubic-bezier(0.22,0.9,0.24,1)] motion-reduce:transition-none"
            >
              <span className="h-7 w-[3px] rounded-r-full bg-primary" />
            </li>
          )}
          {[1, 2, 3].map((n) => (
            <HostRow key={n} n={n} s={s} a={a} host={s.hosts.get(n)} confirm={confirm} rename={rename} recording={recording} setRecording={setRecording} />
          ))}
        </ul>
      </RadioGroupPrimitive.Root>
    </Group>
    <RenameDialog req={renaming} a={a} onClose={() => setRenaming(null)} />
    </>
  );
}

type RowProps = {
  n: number; s: BoardState; a: Actions; host?: Host; confirm: Confirm; rename: (n: number) => void;
  recording: number; setRecording: (n: number) => void;
};

function HostRow({ n, s, a, host, confirm, rename, recording, setRecording }: RowProps) {
  const on = !!host && s.activeHost === n;
  return (
    <li
      data-host={n}
      className={cn('flex items-center gap-2 pr-3 transition-colors', on && 'bg-primary/[0.07]', 'data-live:bg-primary/[0.12]')}
      style={{ height: ROW }}
    >
      <RadioGroupPrimitive.Item
        value={String(n)}
        disabled={host?.state !== 'ready'}
        title={host?.state === 'ready' ? t('hosts.pickTitle') : undefined}
        className="flex h-full min-w-0 flex-1 items-center gap-3 pl-3 text-left outline-none focus-visible:bg-accent enabled:hover:bg-accent/60"
      >
        <HostBadge label={hostLabel(s, n)} on={on} />
        <span className="min-w-0 flex-1 leading-tight">
          <span className={cn('block truncate', host ? 'font-medium' : 'text-muted-foreground')}>
            {host ? host.name || t('hosts.unnamed') : t('hosts.empty')}
          </span>
          {host && <StateTag tone={hostStateTone(host.state) as Tone}>{hostStateText(host.state)}</StateTag>}
        </span>
      </RadioGroupPrimitive.Item>
      <ComboButton n={n} s={s} a={a} rec={recording === n} setRec={(on) => setRecording(on ? n : 0)} />
      {host ? (
        <RowMenu label={t('common.more')}>
          <DropdownMenuItem onSelect={() => rename(n)}>{t('hosts.rename')}</DropdownMenuItem>
          {/* For a computer that forgot the board: it pairs again into the same row */}
          <DropdownMenuItem disabled={s.hostWindow.open} onSelect={a.openHostWindow}>
            {t('hosts.pairAgain')}
          </DropdownMenuItem>
          <DropdownMenuItem disabled={n === 1} onSelect={() => a.moveHost(n, n - 1)}>
            {t('hosts.moveUp')}
          </DropdownMenuItem>
          <DropdownMenuItem disabled={n === 3} onSelect={() => a.moveHost(n, n + 1)}>
            {t('hosts.moveDown')}
          </DropdownMenuItem>
          <DropdownMenuSeparator />
          <DropdownMenuItem
            variant="destructive"
            onSelect={() =>
              confirm({
                title: t('common.remove'),
                body: t('confirm.removeHost', { name: host.name || t('confirm.hostName', { n }), board: s.boardName || 'Blue Baton' }),
                action: () => a.removeHost(n),
              })
            }
          >
            {t('hosts.removeItem')}
          </DropdownMenuItem>
        </RowMenu>
      ) : (
        <Button size="xs" variant="outline" onClick={a.openHostWindow} disabled={s.hostWindow.open}>
          {t('hosts.add')}
        </Button>
      )}
    </li>
  );
}

/*
 * The host's switch combo; click it to record a new one. While recording, key presses in this window are taken
 * (not typed anywhere): Esc or a click anywhere else cancels (the click still does what it does), Backspace alone
 * clears. A combo another host already uses is refused here; the
 * board refuses invalid ones (bad_combo). Pressing an existing combo on a keyboard attached to the board switches
 * hosts instead (the board takes it), so it never reaches this window.
 */
function ComboButton({ n, s, a, rec, setRec }: { n: number; s: BoardState; a: Actions; rec: boolean; setRec: (on: boolean) => void }) {
  const [held, setHeld] = useState<string[]>([]);
  const recorder = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!rec) return;
    const save = (combo: string) => {
      setRec(false);
      a.setCombos([0, 1, 2].map((i) => (i === n - 1 ? combo : s.combos[i] ?? DEFAULT_COMBOS[i])));
    };
    const down = (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();
      const plain = !e.ctrlKey && !e.altKey && !e.shiftKey && !e.metaKey;
      if (plain && e.code === 'Escape') return setRec(false);
      if (plain && (e.code === 'Backspace' || e.code === 'Delete')) return save('');
      const r = comboFromEvent(e);
      if (!r.combo) {
        setHeld(modifiers(e));
        if (r.error) toast(t(r.error === 'key' ? 'combo.badKey' : 'combo.needModifier'), { id: 'combo' });
        return;
      }
      const other = s.combos.findIndex((c, i) => i !== n - 1 && sameCombo(c, r.combo));
      if (other >= 0) {
        toast(t('combo.taken', { n: hostLabel(s, other + 1) }), { id: 'combo' });
        return;
      }
      save(r.combo);
    };
    const up = (e: KeyboardEvent) => setHeld(modifiers(e));
    const outside = (e: PointerEvent) => {
      if (!recorder.current?.contains(e.target as Node)) setRec(false);
    };
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up, true);
    window.addEventListener('pointerdown', outside, true);
    return () => {
      window.removeEventListener('keydown', down, true);
      window.removeEventListener('keyup', up, true);
      window.removeEventListener('pointerdown', outside, true);
    };
  }, [rec, setRec, n, s.combos, a]);
  if (rec) {
    return (
      <button
        ref={recorder}
        type="button"
        autoFocus
        onBlur={() => setRec(false)}
        onClick={() => setRec(false)}
        title={t('combo.recordHint')}
        className="h-7 rounded-md bg-primary/10 px-2 text-xs font-medium text-primary outline-none ring-[1.5px] ring-primary"
      >
        {held.length ? `${comboLabel(held)}…` : t('combo.record')}
      </button>
    );
  }
  return (
    <button
      type="button"
      title={t('combo.change')}
      onClick={() => {
        setHeld([]);
        setRec(true);
      }}
      className="h-7 rounded-md px-2 outline-none hover:bg-accent focus-visible:ring-2 focus-visible:ring-ring"
    >
      <Keycaps combo={s.combos[n - 1]} />
    </button>
  );
}

/*
 * The name shown instead of the host's number, up to 8 characters (what the board stores). Empty shows the number
 * again. Over 8 the Save button is off rather than cutting the text, so typing with an input method (Korean, Japanese)
 * is never broken off in the middle of a character.
 */
const NICK_CHARS = 8;

type Renaming = { n: number; nick: string };

function RenameDialog({ req, a, onClose }: { req: Renaming | null; a: Actions; onClose: () => void }) {
  const [value, setValue] = useState('');
  useEffect(() => {
    if (req) setValue(req.nick);
  }, [req]);
  const n = req?.n ?? 0;
  const nick = value.trim();
  const chars = [...nick].length;
  const ok = chars <= NICK_CHARS;
  const save = async () => {
    if (ok && (await a.renameHost(n, nick))) onClose();
  };
  return (
    <Dialog open={!!req} onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="sm:max-w-xs" aria-describedby={undefined}>
        <DialogHeader>
          <DialogTitle>{t('hosts.renameTitle')}</DialogTitle>
        </DialogHeader>
        <form
          className="relative"
          onSubmit={(e) => {
            e.preventDefault();
            save();
          }}
        >
          <Input
            autoFocus
            value={value}
            placeholder={n ? String(n) : undefined}
            aria-label={t('hosts.renameTitle')}
            aria-invalid={!ok}
            autoCapitalize="none"
            autoCorrect="off"
            autoComplete="off"
            spellCheck={false}
            onChange={(e) => setValue(e.target.value)}
            className="pr-12"
          />
          <span
            className={cn(
              'pointer-events-none absolute right-3 top-1/2 -translate-y-1/2 text-xs tabular-nums',
              ok ? 'text-muted-foreground' : 'text-destructive',
            )}
          >
            {chars}/{NICK_CHARS}
          </span>
        </form>
        <DialogFooter>
          <Button variant="ghost" onClick={onClose}>{t('common.cancel')}</Button>
          <Button disabled={!ok} onClick={save}>{t('common.save')}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
