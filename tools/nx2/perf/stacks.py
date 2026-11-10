import sys, collections, importlib.util, io, contextlib
map_path, raw_path, needle = sys.argv[1], sys.argv[2], sys.argv[3]
depth = int(sys.argv[4]) if len(sys.argv) > 4 else 14
sys.argv = ['mapsym', map_path, raw_path, '0']
spec = importlib.util.spec_from_file_location('mapsym', __file__.replace('stacks.py', 'mapsym.py'))
m = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(m)
c = collections.Counter(); total = 0
for line in open(raw_path, encoding='utf-8', errors='replace'):
    stack, _, n = line.rstrip().rpartition(' ')
    try: n = int(n)
    except ValueError: continue
    fr = stack.split(';'); names = [m.name_of(f) for f in fr[1:]]
    total += n
    if any(needle in x for x in names):
        i = max(i for i, x in enumerate(names) if needle in x)
        # collapse repeats
        chain = []
        for x in names[max(0, i - depth):i + 1]:
            if not chain or chain[-1] != x: chain.append(x)
        c[' > '.join(chain)] += n
print('total samples', total)
for k, v in c.most_common(6):
    print(f'{100*v/total:5.1f}%  {k}\n')
