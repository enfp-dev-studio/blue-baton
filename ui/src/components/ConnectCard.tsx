import { Usb } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { t } from '@/lib/board/i18n.js';
import type { Actions, BoardState, Controller } from '@/lib/board/types';

/* Before the board is linked: the app looks for it by itself; a web page needs a click (Web Serial picker) */
export function ConnectCard({ s, a, ctl }: { s: BoardState; a: Actions; ctl: Controller }) {
  return (
    <Card className="mx-auto mt-[8vh] max-w-md text-center">
      <CardContent className="px-8 py-6">
        <div className="mx-auto mb-4 grid size-13 place-items-center rounded-2xl bg-muted text-muted-foreground">
          <Usb className="size-6" />
        </div>
        <h2 className="text-lg font-semibold">{t('link.disconnected')}</h2>
        <p className="mb-5 mt-1 text-sm text-muted-foreground">{ctl.inApp ? t('link.searching') : t('link.connectHint')}</p>
        {!ctl.inApp && (
          <Button className="rounded-full" onClick={a.connect} disabled={s.link === 'connecting'}>
            <Usb /> {t('link.connect')}
          </Button>
        )}
      </CardContent>
    </Card>
  );
}
