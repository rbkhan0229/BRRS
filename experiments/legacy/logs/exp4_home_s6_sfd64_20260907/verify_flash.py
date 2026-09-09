import argparse,hashlib,json,pathlib,pylink,datetime
root=pathlib.Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--variant',required=True);ap.add_argument('--side',choices=['init','tx'],required=True);a=ap.parse_args()
m=json.loads((root/'manifest.json').read_text());rs=m['variants'][a.variant];roles=['init'] if a.side=='init' else ['N2','N3','N4','N5','N6','N7']
actual={str(x.SerialNumber) for x in pylink.JLink().connected_emulators()};assert actual=={rs[x]['serial'] for x in roles},actual
out={}
for role in roles:
    r=rs[role];p=root/r['hex'];assert hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256']
    memory={};base=0;eof=False
    for line in p.read_text().splitlines():
        raw=bytes.fromhex(line[1:]);assert line.startswith(':') and len(raw)==raw[0]+5 and sum(raw)%256==0
        n=raw[0];addr=int.from_bytes(raw[1:3],'big');typ=raw[3];data=raw[4:4+n]
        if typ==0:
            for i,b in enumerate(data):
                address=base+addr+i;assert address not in memory or memory[address]==b;memory[address]=b
        elif typ==4:base=int.from_bytes(data,'big')<<16
        elif typ==2:base=int.from_bytes(data,'big')<<4
        elif typ==1:eof=True
        elif typ not in (3,5):raise RuntimeError(f'unsupported hex type {typ}')
    assert eof and memory
    addresses=sorted(memory);chunks=[];start=last=addresses[0];buf=bytearray([memory[start]])
    for addr in addresses[1:]:
        if addr==last+1 and len(buf)<4096:buf.append(memory[addr])
        else:chunks.append((start,bytes(buf)));start=addr;buf=bytearray([memory[addr]])
        last=addr
    chunks.append((start,bytes(buf)))
    jl=pylink.JLink();jl.open(serial_no=int(r['serial']));jl.set_tif(pylink.enums.JLinkInterfaces.SWD);jl.connect('NRF52840_XXAA',speed=4000)
    try:
        for addr,data in chunks:
            got=bytes(jl.memory_read8(addr,len(data)));assert got==data,f'{role} flash mismatch at {addr:#x}'
    finally:jl.close()
    out[role]={'serial':r['serial'],'hex_sha256':r['sha256'],'bytes_verified':len(memory),'status':'PASS','method':'readback populated HEX bytes, no halt/reset'}
print(json.dumps({'at':datetime.datetime.now().astimezone().isoformat(),'variant':a.variant,'side':a.side,'boards':out},indent=2))
