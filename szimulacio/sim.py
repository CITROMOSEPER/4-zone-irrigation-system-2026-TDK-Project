# Port of the firmware's Dose / WaterBalance / Scheduler rain rule to Python, run on Open-Meteo daily data.
import json, os, random, sys
HERE = os.path.dirname(os.path.abspath(__file__))
w = json.load(open(os.path.join(HERE, 'wx.json'), encoding='utf-8'))['daily']
days = [dict(d=t, et0=e, rain=r or 0.0, tmax=a, tmin=b) for t, e, r, a, b in zip(
    w['time'], w['et0_fao_evapotranspiration'], w['precipitation_sum'], w['temperature_2m_max'], w['temperature_2m_min'])]

RAIN_EFF = 0.8
RAIN_STOP = 2.0
PROFILES = {
    'pazsit': dict(name='Pázsit, vályog, ködfúvóka', kc=.9, root=15, lo=35, hi=50, fc=27, wp=12, rate=20, eff=70),
    'zoldseg': dict(name='Zöldség, vályog, csepegtető', kc=1.0, root=30, lo=40, hi=60, fc=27, wp=12, rate=6, eff=90),
    'medit': dict(name='Mediterrán, homokos vályog, csepegtető', kc=.5, root=50, lo=20, hi=40, fc=18, wp=8, rate=6, eff=90),
}
MIN_RUN, MAX_RUN, DAILY_CAP = 60, 3600, 90 * 60


def avail(p): return (p['fc'] - p['wp']) / 100 * p['root'] * 10


def dose_sec(p, s):
    a = avail(p)
    mm = max(0, (p['hi'] - s) / 100 * a)
    gross = mm / (p['eff'] / 100)
    sec = round(gross / p['rate'] * 3600 / 10) * 10
    return min(sec, MAX_RUN)


def net_mm(p, sec): return p['rate'] * sec / 3600 * p['eff'] / 100


def season(y): return [d for d in days if f'{y}-04-01' <= d['d'] <= f'{y}-09-30']


def timer_sec(p):
    # "set and forget": sized once to the mean July demand of the whole data set, every second day
    jul = [d['et0'] for d in days if d['d'][5:7] == '07']
    etc = sum(jul) / len(jul) * p['kc']
    gross = etc * 2 / (p['eff'] / 100)
    return round(gross / p['rate'] * 3600 / 10) * 10


def run(p, strategy, y, kc_err=1.0, rain_err=1.0, sensor_sd=0.0, seed=1, trace=None):
    rnd = random.Random(seed)
    a = avail(p)
    truth = est = float(p['hi'])
    tsec = timer_sec(p)
    stats = dict(crit=0, gross=0.0, drain=0.0, runs=0, below=0, skips_rain=0, skips_wet=0, sec=0, n=0)
    for i, d in enumerate(season(y)):
        due = strategy in ('A_LOOP', 'Z_LOOP') or i % 2 == 0
        sec = 0
        if due:
            if strategy == 'TIMER':
                sec = tsec
            elif strategy == 'SCHED':
                if d['rain'] >= RAIN_STOP: stats['skips_rain'] += 1
                else: sec = tsec
            elif strategy == 'A_LOOP':          # open loop: estimate, rain skip
                if d['rain'] >= RAIN_STOP: stats['skips_rain'] += 1
                elif est >= p['lo']: stats['skips_wet'] += 1
                else: sec = max(dose_sec(p, est), MIN_RUN)
            elif strategy == 'Z_LOOP':          # closed loop: sensor, no rain skip
                meas = truth + rnd.gauss(0, sensor_sd)
                if meas >= p['lo']: stats['skips_wet'] += 1
                else: sec = max(dose_sec(p, meas), MIN_RUN)
        sec = min(sec, DAILY_CAP)
        if sec:
            n = net_mm(p, sec)
            stats['gross'] += p['rate'] * sec / 3600
            stats['sec'] += sec
            stats['runs'] += 1
            est = min(100, est + n / a * 100)
            t2 = truth + n / a * 100
            if t2 > 100: stats['drain'] += (t2 - 100) / 100 * a; t2 = 100
            truth = t2
        # end of the day: firmware model and "real" soil (the same unless an error factor is set)
        est = max(0, min(100, est + (d['rain'] * RAIN_EFF - d['et0'] * p['kc']) / a * 100))
        t2 = truth + (d['rain'] * RAIN_EFF * rain_err - d['et0'] * p['kc'] * kc_err) / a * 100
        if t2 > 100: stats['drain'] += (t2 - 100) / 100 * a; t2 = 100
        truth = max(0, t2)
        stats['n'] += 1
        if truth < p['lo']: stats['below'] += 1
        if truth < p['lo'] - 10: stats['crit'] += 1
        if trace is not None: trace.append((d['d'], truth, sec, d['rain']))
    return stats


if __name__ == '__main__':
    out = {'climate': {}, 'res': {}, 'sens': {}, 'timer_min': {}}
    for y in range(2022, 2026):
        s = season(y)
        out['climate'][y] = dict(et0=sum(d['et0'] for d in s), rain=sum(d['rain'] for d in s),
                                 raindays=sum(d['rain'] >= 2 for d in s))
    for k, p in PROFILES.items():
        out['timer_min'][k] = timer_sec(p) / 60
        out['res'][k] = {}
        for st in ('TIMER', 'SCHED', 'A_LOOP', 'Z_LOOP'):
            out['res'][k][st] = {y: run(p, st, y, sensor_sd=3.0 if st == 'Z_LOOP' else 0) for y in range(2022, 2026)}
    p = PROFILES['pazsit']
    for name, ke, re_ in (('alap', 1, 1), ('ET +15%', 1.15, 1), ('ET -15%', .85, 1), ('eső -25%', 1, .75)):
        out['sens'][name] = {st: {y: run(p, st, y, ke, re_, 3.0 if st == 'Z_LOOP' else 0) for y in range(2022, 2026)}
                             for st in ('TIMER', 'A_LOOP', 'Z_LOOP')}
    tr = {}
    for st in ('TIMER', 'A_LOOP', 'Z_LOOP'):
        t = []; run(p, st, 2022, sensor_sd=3.0 if st == 'Z_LOOP' else 0, trace=t); tr[st] = t
    out['trace'] = tr
    json.dump(out, open(os.path.join(HERE, 'sim.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print(out['climate'], out['timer_min'])
    for k in out['res']:
        print(k, avail(PROFILES[k]))
        for st, r in out['res'][k].items():
            g = [r[y]['gross'] for y in r]; b = [r[y]['below'] for y in r]; dr = [r[y]['drain'] for y in r]; ru=[r[y]['runs'] for y in r]
            print(f"  {st:7} gross {[round(x) for x in g]} avg {sum(g)/4:.0f} drain {sum(dr)/4:.0f} below {b} runs {ru}")
    for n, v in out['sens'].items():
        print(n, {st: (round(sum(r[y]['gross'] for y in r)/4), sum(r[y]['below'] for y in r)/4) for st, r in v.items()})

