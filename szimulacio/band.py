# Kiegészítő számítások a sim.py modelljével: lo-hi sáv hatása (dolgozat 16. táblázat) és a zajmag érzékenysége.
# Kritikus nap itt rögzítetten 25% alatti nap, hogy a sávváltozatok összevethetők legyenek.
import json, os
from sim import PROFILES, run, HERE

YEARS = range(2022, 2026)


def band_row(lo, hi, st):
    p = dict(PROFILES['pazsit'], lo=lo, hi=hi)
    gross = crit = 0.0
    for y in YEARS:
        t = []
        s = run(p, st, y, sensor_sd=3.0 if st == 'Z_LOOP' else 0, trace=t)
        gross += s['gross']
        crit += sum(1 for _, truth, _, _ in t if truth < 25)
    return gross / 4, crit / 4


def seed_row(k, seed):
    r = [run(PROFILES[k], 'Z_LOOP', y, sensor_sd=3.0, seed=seed) for y in YEARS]
    return sum(s['gross'] for s in r) / 4, sum(s['crit'] for s in r) / 4


if __name__ == '__main__':
    out = {'band': {}, 'seed': {}}
    for lo, hi in ((35, 50), (45, 60), (50, 70)):
        out['band'][f'{lo}-{hi}'] = {st: band_row(lo, hi, st) for st in ('A_LOOP', 'Z_LOOP')}
    for k in PROFILES:
        rows = [seed_row(k, s) for s in range(1, 21)]
        g, c = [x[0] for x in rows], [x[1] for x in rows]
        out['seed'][k] = dict(gross_min=min(g), gross_max=max(g), gross_avg=sum(g) / 20, crit_min=min(c), crit_max=max(c), crit_avg=sum(c) / 20)
    json.dump(out, open(os.path.join(HERE, 'band.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(json.dumps(out, ensure_ascii=False, indent=1))
