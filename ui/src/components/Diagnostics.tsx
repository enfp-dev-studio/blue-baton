import { useEffect, useRef, useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { Card, CardContent } from '@/components/ui/card';
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

function useVisible() {
  const [visible, setVisible] = useState(() => document.visibilityState === 'visible');
  useEffect(() => {
    const on = () => setVisible(document.visibilityState === 'visible');
    document.addEventListener('visibilitychange', on);
    return () => document.removeEventListener('visibilitychange', on);
  }, []);
  return visible;
}

/*
 * Developer area: the input monitor (plain DOM, updated dozens of times a second) and the protocol log.
 * The board sends input content only while the monitor is open and the window is visible.
 */
export function Diagnostics({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  const [open, setOpen] = useState(() => stored('bb.diagOpen'));
  const [boardLines, setBoardLines] = useState(false);
  const holder = useRef<HTMLDivElement>(null);
  const visible = useVisible();
  useEffect(() => {
    if (open && holder.current) holder.current.replaceChildren(ctl.monitorRoot);
  }, [open, ctl.monitorRoot]);
  useEffect(() => {
    a.monitor(open && visible);
  }, [a, open, visible]);
  const rows = open ? s.log.filter((r) => boardLines || r.dir !== '·').slice(-200) : [];
  return (
    <Collapsible
      open={open}
      onOpenChange={(v) => {
        setOpen(v);
        store('bb.diagOpen', v);
      }}
    >
      <Card className="gap-0 py-0">
        <CollapsibleTrigger className="flex w-full items-center justify-between rounded-xl px-5 py-4 text-left font-semibold text-muted-foreground outline-none focus-visible:ring-2 focus-visible:ring-ring">
          {t('diag.title')}
          <ChevronDown className={cn('size-4 transition-transform', open && 'rotate-180')} />
        </CollapsibleTrigger>
        <CollapsibleContent>
          <CardContent className="grid gap-6 px-5 pb-5">
            <section>
              <h3 className="mb-1 font-semibold">{t('inputs.title')}</h3>
              <p className="mb-3 text-sm text-muted-foreground">{t('inputs.hint')}</p>
              <div ref={holder} />
            </section>
            <section className="border-t pt-5">
              <div className="mb-2 flex flex-wrap items-center justify-between gap-2">
                <h3 className="font-semibold">{t('log.title')}</h3>
                <div className="flex items-center gap-2">
                  <Checkbox id="log-board" checked={boardLines} onCheckedChange={(v) => setBoardLines(v === true)} />
                  <Label htmlFor="log-board" className="text-sm font-normal text-muted-foreground">{t('log.boardLines')}</Label>
                </div>
              </div>
              <div className="max-h-80 overflow-auto whitespace-pre-wrap break-all rounded-lg bg-muted p-2.5 font-mono text-xs">
                {rows.map((r, i) => (
                  <div key={i} className={cn(r.dir === '>' && 'text-primary', r.dir === '·' && 'text-muted-foreground')}>
                    {`${r.at.toLocaleTimeString()} ${r.dir} ${r.text}`}
                  </div>
                ))}
              </div>
            </section>
          </CardContent>
        </CollapsibleContent>
      </Card>
    </Collapsible>
  );
}
