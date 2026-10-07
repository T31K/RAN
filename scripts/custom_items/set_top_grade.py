"""Let every weapon/armor be upgraded past +9: set SGRINDING::emGRINDER_TYPE = EMGRINDER_TOP on all
suit items in item.isf.

Grinding past +9 (GRADE_HIGH) needs the TOP flag on the item itself (GLCharactorReq::ReqInvenGrinding
client-side, GLChar::MsgReqInvenGrinding server-side), and only ~3100 of ~7300 items had it. The client
refusal key GRINDING_NOT_BESTITEM is missing from gameintext, so the player sees no message at all.

usage: python3 set_top_grade.py <item.isf>   (in place; patch both server and client glogic.rcc)
"""
import struct
import sys

import itemisf

FILE_SGRINDING = 5
EMGRINDER_TOP = 2


def main(path):
    raw = open(path, 'rb').read()
    head, items, tail = itemisf.parse(raw)
    assert itemisf.serialize(head, items, tail) == raw, 'item.isf round-trip mismatch'
    n = 0
    for it in items:
        if not any(c[0] == itemisf.FILE_SSUIT for c in it.chunks):
            continue
        for c in it.chunks:
            if c[0] == FILE_SGRINDING and len(c[2]) >= 12 and struct.unpack_from('<I', c[2], 4)[0] != EMGRINDER_TOP:
                c[2] = c[2][:4] + struct.pack('<I', EMGRINDER_TOP) + c[2][8:]
                n += 1
    open(path, 'wb').write(itemisf.serialize(head, items, tail))
    print('flagged', n)


if __name__ == '__main__':
    main(sys.argv[1])
