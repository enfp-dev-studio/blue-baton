// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useRef, useState } from 'react';
import { ChevronRight } from 'lucide-react';
import { Checkbox } from '@/components/ui/checkbox';
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from '@/components/ui/collapsible';
import { Label } from '@/components/ui/label';
import { t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';
import { cn } from '@/lib/utils';

function stored(key: string) {
  try {
    return localStorage.getItem(key) === '1';
  } catch {
    return false;
  }
}

function store(key: string, on: boolean) {
  try {
    localStorage.setItem(key, on ? '1' : '0');
  } catch {
    /* storage can be unavailable (private windows) */
  }
}

/*
 * Developer area: the input monitor (plain DOM, updated dozens of times a second) and the protocol log.
 * The board sends input content only while the monitor is open and the window is visible.
 */
export function Diagnostics({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  const [open, setOpen] = useState(() => stored('bb.diagOpen'));
  const [boardLines, setBoardLines] = useState(false);
  const holder = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (open && holder.current) holder.current.replaceChildren(ctl.monitorRoot);
  }, [open, ctl.monitorRoot]);
  useEffect(() => {
    a.monitor(open && s.shown);
  }, [a, open, s.shown]);
  const rows = open ? s.log.filter((r) => boardLines || r.dir !== '·').slice(-200) : [];
  return (
    <Collapsible
      open={open}
      onOpenChange={(v) => {
        setOpen(v);
        store('bb.diagOpen', v);
      }}
    >
      <div className="overflow-hidden rounded-lg border bg-card">
        <CollapsibleTrigger className="flex h-11 w-full items-center justify-between px-4 text-left text-ui font-medium outline-none hover:bg-accent/60 focus-visible:bg-accent">
          {t('diag.title')}
          <ChevronRight className={cn('size-4 text-muted-foreground transition-transform', open && 'rotate-90')} />
        </CollapsibleTrigger>
        <CollapsibleContent>
          <div className="grid gap-5 border-t px-4 py-4">
            <section>
              <h3 className="mb-0.5 font-semibold">{t('inputs.title')}</h3>
              <p className="mb-3 text-xs text-muted-foreground">{t('inputs.hint')}</p>
              <div ref={holder} />
            </section>
            <section className="border-t pt-4">
              <div className="mb-2 flex flex-wrap items-center justify-between gap-2">
                <h3 className="font-semibold">{t('log.title')}</h3>
                <div className="flex items-center gap-2">
                  <Checkbox id="log-board" checked={boardLines} onCheckedChange={(v) => setBoardLines(v === true)} />
                  <Label htmlFor="log-board" className="text-xs font-normal text-muted-foreground">{t('log.boardLines')}</Label>
                </div>
              </div>
              <div className="max-h-80 overflow-auto whitespace-pre-wrap break-all rounded-md bg-muted p-2.5 font-mono text-2xs">
                {rows.map((r, i) => (
                  <div key={i} className={cn(r.dir === '>' && 'text-primary', r.dir === '·' && 'text-muted-foreground')}>
                    {`${r.at.toLocaleTimeString()} ${r.dir} ${r.text}`}
                  </div>
                ))}
              </div>
            </section>
          </div>
        </CollapsibleContent>
      </div>
    </Collapsible>
  );
}
