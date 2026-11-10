"""leaf.py <map> <raw> <thread> : for samples whose leaf is outside the exe (waits, driver, runtime),
which of our functions they were called from (innermost 3 of ours)."""
import sys, collections, importlib.util, io, contextlib
map_path, raw_path, thread = sys.argv[1], sys.argv[2], sys.argv[3]
sys.argv = ['mapsym', map_path, raw_path, '0']
spec = importlib.util.spec_from_file_location('mapsym', __file__.replace('leaf.py', 'mapsym.py'))
m = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(m)
c = collections.Counter(); total = 0
for line in open(raw_path, encoding='utf-8', errors='replace'):
    stack, _, n = line.rstrip().rpartition(' ')
    try: n = int(n)
    except ValueError: continue
    fr = stack.split(';')
    if fr[0] != thread: continue
    total += n
    raw_leaf = fr[-1]
    if raw_leaf.startswith('cheezyemu.exe+') or raw_leaf.startswith('jit+'): continue
    names = [m.name_of(f) for f in fr[1:]]
    ours = [x for f, x in zip(fr[1:], names) if f.startswith('cheezyemu.exe+')]
    leafmod = raw_leaf.split('+')[0] if '+0x' in raw_leaf else raw_leaf
    # the first non-exe frame after the last exe frame names the API
    idx = max((i for i, f in enumerate(fr[1:]) if f.startswith('cheezyemu.exe+')), default=-1)
    api = names[idx + 1] if idx + 1 < len(names) else leafmod
    key = ' > '.join(dict.fromkeys(ours[-3:])) + '  ==>  ' + api + ' ... ' + names[-1]
    c[key] += n
print(thread, 'samples', total)
for k, v in c.most_common(16):
    print(f'{100*v/total:5.1f}%  {k[:230]}')
