import struct
def tokens(d):
    p=16; n=len(d)
    while p<n:
        c=struct.unpack_from('<H',d,p)[0]; p+=2
        if c==1: l=struct.unpack_from('<I',d,p)[0]; p+=4; yield('NAME',d[p:p+l].decode('latin1')); p+=l
        elif c==2: l=struct.unpack_from('<I',d,p)[0]; p+=4; s=d[p:p+l].decode('latin1'); p+=l+2; yield('STR',s)
        elif c==3: yield('INT',struct.unpack_from('<I',d,p)[0]); p+=4
        elif c==5: yield('GUID',None); p+=16
        elif c==6: k=struct.unpack_from('<I',d,p)[0]; p+=4; yield('ILIST',list(struct.unpack_from('<%dI'%k,d,p))); p+=4*k
        elif c==7: k=struct.unpack_from('<I',d,p)[0]; p+=4; yield('FLIST',list(struct.unpack_from('<%df'%k,d,p))); p+=4*k
        elif c==10: yield('{',None)
        elif c==11: yield('}',None)
        else: yield('X',c)
class Node:
    def __init__(s,typ,name): s.typ=typ; s.name=name; s.kids=[]; s.data=[]
def parse(path):
    toks=[t for t in tokens(open(path,'rb').read()) if t[0]!='X']
    root=Node('root',None); stack=[root]; i=0; pend=[]
    while i<len(toks):
        t,v=toks[i]
        if t=='NAME': pend.append(v)
        elif t=='{':
            typ=pend[0] if pend else None; name=pend[1] if len(pend)>1 else None
            n=Node(typ,name); stack[-1].kids.append(n); stack.append(n); pend=[]
        elif t=='}': stack.pop(); pend=[]
        else: stack[-1].data.append((t,v)); pend=[]
        i+=1
    return root
def find_path(node,name,path=()):
    for k in node.kids:
        if k.typ=='Frame':
            p=path+(k,)
            if k.name==name: return p
            r=find_path(k,name,p)
            if r: return r
    return None
def matrix(frame):
    for k in frame.kids:
        if k.typ=='FrameTransformMatrix': return [x for t,v in k.data if t=='FLIST' for x in v]
