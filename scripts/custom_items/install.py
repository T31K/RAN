"""Install the custom items into a client game folder (idempotent).

  install.py <game-dir>      e.g. ~/Projects/RAN/client (what the native app bundles)

  data/glogic/glogic.rcc         item.isf + ItemStrTable.txt   (add_items.py)
  data/skinobject/skinobject.rcc += <frame>_{M,W}.cps
  data/skin/                     += s_{m,w}_<tag>.X
  textures/item/                 += <tex>.dds, <tex>_s.dds
Run make_samehada.py first (assets in port/build/custom_items). The server needs only the
glogic.rcc part: add_items.py /opt/ran/game-client/data/glogic/glogic.rcc, then restart it.
"""
import os
import shutil
import sys
import zipfile
import add_items
from make_samehada import OUT, VARIANTS
from rcc import rewrite_zip


def main():
    game = sys.argv[1]
    add_items.apply(f'{game}/data/glogic/glogic.rcc', f'{game}/data/glogic/glogic.rcc')
    skinobj = f'{game}/data/skinobject/skinobject.rcc'
    rewrite_zip(skinobj, {f'{frame}_{s}.cps': open(f'{OUT}/{frame}_{s}.cps', 'rb').read()
                          for _, _, frame, _, _, _ in VARIANTS for s in 'MW'})
    with zipfile.ZipFile(skinobj) as z:
        assert z.testzip() is None
    for _, _, _, tag, tex, _ in VARIANTS:
        for sex in 'mw':
            shutil.copy2(f'{OUT}/s_{sex}_{tag}.X', f'{game}/data/skin/')
        for f in (f'{tex}.dds', f'{tex}_s.dds'):
            shutil.copy2(f'{OUT}/{f}', f'{game}/textures/item/')
    print('installed into', game)


if __name__ == '__main__':
    main()
