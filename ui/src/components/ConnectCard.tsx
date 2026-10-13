// SPDX-License-Identifier: GPL-3.0-only
import { Usb } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';

/* Before the board is linked: the app looks for it by itself; a web page needs a click (Web Serial picker) */
export function ConnectCard({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  return (
    <div className="mx-auto mt-[10vh] max-w-sm text-center">
      <div className="mx-auto mb-3 grid size-11 place-items-center rounded-xl bg-muted text-muted-foreground">
        <Usb className="size-5" />
      </div>
      <h2 className="text-[15px] font-semibold">{t('link.disconnected')}</h2>
      <p className="mb-4 mt-1 text-xs text-muted-foreground">{ctl.inApp ? t('link.searching') : t('link.connectHint')}</p>
      {!ctl.inApp && (
        <Button size="sm" onClick={a.connect} disabled={s.link === 'connecting'}>
          <Usb /> {t('link.connect')}
        </Button>
      )}
    </div>
  );
}
