# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Losslessly pack the fixed factory resource; never process user banks."""
from collections import Counter
from pathlib import Path
import argparse,hashlib,re
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',(root/'app/factory_dx7.inc').read_text()))
assert hashlib.sha256(raw).hexdigest()=='7f097473ecf71d93bd0d4f786d34d90871b70e980bc77a294c7b0da0274c23db'
order=[field+17*op for field in range(17) for op in range(6)]+list(range(102,128))
voices=[[raw[i+k] for k in order] for i in range(0,len(raw),128)]
pairs=[]
for token in range(128,256):
    counts=Counter((a,b) for v in voices for a,b in zip(v,v[1:]))
    pair,count=counts.most_common(1)[0]
    assert count>=3
    pairs.append(pair)
    for j,voice in enumerate(voices):
        result=[];i=0
        while i<len(voice):
            if i+1<len(voice) and tuple(voice[i:i+2])==pair:result.append(token);i+=2
            else:result.append(voice[i]);i+=1
        voices[j]=result
offsets=[0];packed=[]
depth=[0]*128
for left,right in pairs:depth.append(1+max(depth[left],depth[right]))
for i,voice in enumerate(voices):
    decoded=[];stack=list(reversed(voice))
    while stack:
        value=stack.pop()
        if value<128:decoded.append(value)
        else:stack.extend(reversed(pairs[value-128]))
    restored=bytearray(128)
    for k,value in zip(order,decoded):restored[k]=value
    assert restored==raw[i*128:(i+1)*128]
    packed.extend(voice);offsets.append(len(packed))
text='''/* SPDX-License-Identifier: Apache-2.0
 * Upstream resource notice retained; M-VAVE extraction/packing, 2026.
 * Lossless packing of factory_dx7.inc; regenerate with tools/Pack-Factory.py.
 * Source, voice notices and modifications: THIRD_PARTY.md. */
'''
def array(name,values,kind):
    return 'static const '+kind+' '+name+'['+str(len(values))+'] = {\n'+''.join('    '+','.join(map(str,values[i:i+16]))+',\n' for i in range(0,len(values),16))+'};\n'
text+=f'enum {{ FACTORY_STACK = {max(depth)+1} }};\n'
text+=array('factory_pairs',[v for pair in pairs for v in pair],'uint8_t')
text+=array('factory_offsets',offsets,'uint16_t')
text+=array('factory_tokens',packed,'uint8_t')
target=root/'app/factory_dx7_packed.inc'
if args.check:assert target.read_text()==text,'Regenerate factory_dx7_packed.inc'
else:target.write_text(text,encoding='utf-8',newline='\n')
print(f'PASS 128 voices: {len(raw)} -> {len(packed)+len(offsets)*2+len(pairs)*2} ROM bytes; decode stack {max(depth)+1} bytes; exact roundtrip')
