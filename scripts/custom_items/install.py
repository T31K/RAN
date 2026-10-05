"""Install the custom items into a client game folder (idempotent).

  install.py <game-dir>      e.g. ~/Projects/RAN/client (what the native app bundles)

  data/glogic/glogic.rcc         item.isf + ItemStrTable.txt   (add_items.py)
  data/skinobject/skinobject.rcc += <frame>_{M,W}.cps
  data/skin/                     += s_{m,w}_<tag>.X
  textures/item/                 += <tex>.dds, <tex>_s.dds
  textures/gui/                  += custom_items.dds (inventory icons)
Run make_samehada.py, make_gauntlet.py and render_icons.py first (assets in port/build/custom_items). The server needs only the
glogic.rcc part: add_items.py /opt/ran/game-client/data/glogic/glogic.rcc, then restart it.
"""
import os
import shutil
import sys
import zipfile
import add_items
import make_gauntlet
import make_samehada
from make_samehada import OUT
from rcc import rewrite_zip

# (frame, mesh tag, texture stem) of every custom model
PIECES = [(frame, tag, tex) for _, _, frame, tag, tex, _ in make_samehada.VARIANTS] + \
         [(frame, tag, tex) for _, _, frame, tag, tex, _, _ in make_gauntlet.VARIANTS]


def main():
    game = sys.argv[1]
    add_items.apply(f'{game}/data/glogic/glogic.rcc', f'{game}/data/glogic/glogic.rcc')
    skinobj = f'{game}/data/skinobject/skinobject.rcc'
    rewrite_zip(skinobj, {f'{frame}_{s}.cps': open(f'{OUT}/{frame}_{s}.cps', 'rb').read()
                          for frame, _, _ in PIECES for s in 'MW'})
    with zipfile.ZipFile(skinobj) as z:
        assert z.testzip() is None
    for _, tag, tex in PIECES:
        for sex in 'mw':
            shutil.copy2(f'{OUT}/s_{sex}_{tag}.X', f'{game}/data/skin/')
        for f in (f'{tex}.dds', f'{tex}_s.dds'):
            shutil.copy2(f'{OUT}/{f}', f'{game}/textures/item/')
    shutil.copy2(f'{OUT}/{add_items.ICON_SHEET}', f'{game}/textures/gui/')   # render_icons.py
    print('installed into', game)


if __name__ == '__main__':
    main()
