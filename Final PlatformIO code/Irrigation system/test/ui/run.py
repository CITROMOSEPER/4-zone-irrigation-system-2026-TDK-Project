"""Browser tests for the web UI, against the offline demo (portal/app-demo.html) and the setup page.

    test/ui/run.sh                 (or: python3 test/ui/run.py)

Needs Python 3 with Playwright (pip install playwright; playwright install chromium).
Every page, sheet and setting is exercised in English and Hungarian, light and dark, on a phone and a desktop
viewport. Any JavaScript error fails the run. Screenshots of every page go to test/ui/shots/.
"""
import json, pathlib, re, sys, time
from playwright.sync_api import sync_playwright

ROOT = pathlib.Path(__file__).resolve().parents[2]
APP = (ROOT / "portal" / "app-demo.html").as_uri()
SETUP = (ROOT / "portal" / "index.html").as_uri()
SHOTS = pathlib.Path(__file__).with_name("shots")
SHOTS.mkdir(exist_ok=True)
PHONE = {"width": 390, "height": 844}
DESK = {"width": 1280, "height": 860}

checks = fails = 0
def check(name, ok, info=""):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", name, info)

class Page:
    """One browser tab on the demo, collecting JavaScript errors."""
    def __init__(self, browser, lang="en", theme="light", vp=PHONE):
        self.ctx = browser.new_context(viewport=vp, locale="hu-HU" if lang == "hu" else "en-GB", color_scheme=theme)
        self.p = self.ctx.new_page()
        self.errors = []
        self.p.on("pageerror", lambda e: self.errors.append(str(e)))
        self.p.on("console", lambda m: self.errors.append(m.text) if m.type == "error" else None)
        self.lang = lang
    def go(self, hash_="", wait=1500):
        self.p.goto(APP + hash_)
        self.p.wait_for_timeout(wait)
    def js(self, x):
        return self.p.evaluate(x)
    def txt(self, sel):
        return self.p.inner_text(sel)
    def wait(self, ms):
        self.p.wait_for_timeout(ms)
    def open_params(self):
        self.js("document.querySelectorAll('#zpg details').forEach(function(d){d.open=true})")
        self.wait(150)
    def sheet_open(self):
        return self.js("document.getElementById('sh').className.indexOf('on')>=0")
    def close_sheet(self):
        self.p.keyboard.press("Escape")
        self.wait(400)
    def toast(self):
        return self.txt("#ts")
    def shot(self, name, full=True):
        self.p.screenshot(path=str(SHOTS / (name + ".png")), full_page=full)
    def done(self, tag):
        check(tag + ": no JavaScript errors", not self.errors, self.errors[:3])
        self.ctx.close()

# ---------------------------------------------------------------- home
def test_home(b):
    # skeleton: four cards in the real cards' shape, before any answer (the page without the mock, so nothing answers)
    s = Page(b)
    s.p.goto((ROOT / "portal" / "app.html").as_uri())
    s.wait(300)
    sk = s.js("Array.from(document.querySelectorAll('#zl .zc.sk')).map(function(c){return Math.round(c.getBoundingClientRect().height)})")
    s.ctx.close()
    t = Page(b)
    check("home: 4 skeleton cards", len(sk) == 4, sk)
    t.go()
    real = t.js("Array.from(document.querySelectorAll('#zl .zc')).map(function(c){return Math.round(c.getBoundingClientRect().height)})")
    check("home: 4 zone cards", len(real) == 4, real)
    check("home: skeleton cards as tall as the real ones", sk and real and sk[0] == real[0], (sk, real))
    check("home: summary shows temperature, humidity, tank", t.js("document.querySelectorAll('#meta>div').length") >= 3)
    check("home: summary has values", re.search(r"\d", t.txt("#meta")) is not None, t.txt("#meta"))
    # the card name opens the zone
    t.p.locator("#zl .zc .zl").first.click(); t.wait(800)
    check("home: card name opens the zone page", t.js("location.hash") == "#z1")
    t.go()
    # switching a zone off offers Undo, and Undo turns it back on
    sw = t.p.locator("#zl .zc .sw").first
    sw.click(); t.wait(600)
    check("home: zone switch shows an Undo toast", t.p.locator("#ts .tsu").count() == 1, t.toast())
    t.p.locator("#ts .tsu").click(); t.wait(2500)
    check("home: Undo turns the zone back on", t.js("window.__MOCK.z[0].en") == 1)
    # Water now: a disabled zone refuses at once, an enabled one opens the length sheet
    t.p.locator("#zl .zc").nth(3).locator(".btn.wa").click(); t.wait(300)
    check("home: Water now on a disabled zone refuses at once", not t.sheet_open() and t.toast() != "", t.toast())
    t.p.locator("#zl .zc").nth(1).locator(".btn.wa").click(); t.wait(500)
    check("home: Water now opens the length sheet", t.sheet_open())
    rows = t.p.locator("#shb .row")
    check("home: the sheet lists lengths", rows.count() >= 6, rows.count())
    t.p.locator("#shb .row", has_text=re.compile(r"30 s")).first.click(); t.wait(2500)
    check("home: watering starts", t.js("window.__MOCK.az") == 1, t.js("window.__MOCK.az"))
    check("home: the card shows it", "Watering" in t.txt("#zl .zc:nth-child(2)"), t.txt("#zl .zc:nth-child(2)"))
    # Water now again on the running zone: Stop
    t.p.locator("#zl .zc").nth(1).locator(".btn.wa").click(); t.wait(400)
    t.p.locator("#shb .row").first.click(); t.wait(2500)
    check("home: Stop ends the run", t.js("window.__MOCK.az") == -1)
    # DND on a zone
    t.p.locator("#zl .zc").nth(0).locator(".btn.dn").click(); t.wait(400)
    check("home: DND sheet opens", t.sheet_open())
    t.p.locator("#shb .row").first.click(); t.wait(2200)
    check("home: DND set on the zone", t.js("window.__MOCK.z[0].dnd>0") is True)
    t.shot("home-phone")
    # the next automatic watering: the expected length from the forecast (loop modes)
    check("home: expected length on a weather-based card", "Tomorrow 06:40 · about" in t.txt("#zl .zc:nth-child(2)") or "· about" in t.txt("#zl .zc:nth-child(2)"), t.txt("#zl .zc:nth-child(2)"))
    check("home: 'probably not needed' when moist", "probably not needed" in t.txt("#zl .zc:nth-child(3)"), t.txt("#zl .zc:nth-child(3)"))
    # the whole card opens the zone: its texts too, not its switch or buttons
    card = t.p.locator("#zl .zc").nth(2)
    card.locator(".sw").click(); t.wait(300)
    check("home: the card's switch does not open the zone", t.js("location.hash") in ("", "#"), t.js("location.hash"))
    card.locator(".sw").click(); t.wait(1500)
    t.done("home")
    # on a touch screen (the name's invisible overlay did not reach the soil bar and the lines below it)
    ctx = b.new_context(viewport=PHONE, has_touch=True, is_mobile=True); p = ctx.new_page()
    for sel in (".smh", ".bar", ".ln dd", ".zm"):
        p.goto(APP); p.wait_for_timeout(1200)
        e = p.locator("#zl .zc").nth(2).locator(sel).first; e.scroll_into_view_if_needed(); p.wait_for_timeout(300)
        bb = e.bounding_box(); p.touchscreen.tap(bb["x"] + bb["width"] * 0.3, bb["y"] + bb["height"] / 2); p.wait_for_timeout(500)
        check("home: tapping %s opens the zone" % sel, p.evaluate("location.hash") == "#z3", p.evaluate("location.hash"))
    ctx.close()

# ---------------------------------------------------------------- zone page
def test_zone(b):
    t = Page(b)
    t.go("#z1", 2500)
    check("zone: name field", t.p.input_value("#zname") != "")
    check("zone: status rows", t.p.locator("#znow .row").count() >= 3)
    check("zone: soil chart drawn", t.p.locator("#zcl canvas").count() == 1)
    # mode switch
    t.p.locator("#zms button").nth(1).click(); t.wait(1200)
    check("zone: mode changes to closed loop", t.js("window.__MOCK.z[0].m") == 1)
    t.p.locator("#zms button").nth(0).click(); t.wait(1200)
    # rename
    t.p.fill("#zname", "Front lawn 2"); t.p.press("#zname", "Enter"); t.wait(1200)
    check("zone: rename saved", t.js("window.__MOCK.z[0].name") == "Front lawn 2", t.js("window.__MOCK.z[0].name"))
    t.p.fill("#zname", "Front lawn"); t.p.press("#zname", "Enter"); t.wait(800)
    # calendar: add a one-off watering on the selected day
    n0 = t.js("window.__MOCK.z[0].sch.length")
    t.p.locator("#zday .btn.wa").click(); t.wait(400)
    check("zone: new watering sheet", t.sheet_open())
    t.p.locator("#shb .btn.wap").click(); t.wait(1500)
    check("zone: watering added to the calendar", t.js("window.__MOCK.z[0].sch.length") == n0 + 1)
    # parameters: preset, growth stages, planting date, the stage table, season end, soil depth, sprinkler
    t.open_params()
    sel = t.p.locator("#zpg details").nth(0).locator("select")
    sel.select_option("fruitv"); t.wait(1200); t.open_params()
    check("zone: crop preset turns growth stages on", t.p.is_checked("#p_plant_st"))
    check("zone: planting date set to today", t.p.input_value("#p_plant_pd") != "")
    vrow = t.p.locator("#zpg details").nth(0).locator("button.row.go")
    if vrow.get_attribute("aria-expanded") != "true":
        vrow.click(); t.wait(300); t.open_params()
    check("zone: stage table shown", t.p.locator("#p_plant_l0").count() == 1)
    check("zone: the Values detail line says it is the current state", t.txt("#zgr").startswith("Now:"), t.txt("#zgr"))
    t.p.fill("#p_plant_kc", "1,2"); t.p.press("#p_plant_kc", "Enter"); t.wait(1200)
    check("zone: Kc typed with a decimal comma", abs(t.js("window.__MOCK.z[0].plant.kc") - 1.2) < 1e-6, t.js("window.__MOCK.z[0].plant.kc"))
    t.open_params()
    check("zone: editing a value makes the plant Custom", t.js("window.__MOCK.z[0].plant.p") == "")
    t.p.locator("#p_plant_oe").click(); t.wait(800)
    check("zone: season-end switch saved", t.js("window.__MOCK.z[0].plant.oe") == 1)
    t.open_params()
    t.p.locator("#p_plant_st").click(); t.wait(1000); t.open_params()
    check("zone: growth stages off greys the planting date", t.p.is_disabled("#p_plant_pd"))
    t.p.fill("#p_soil_depth", "10"); t.p.press("#p_soil_depth", "Enter"); t.wait(1000)
    check("zone: soil depth saved", t.js("window.__MOCK.z[0].soil_.depth") == 10)
    t.open_params()
    t.p.fill("#p_spr_rate", "100"); t.p.press("#p_spr_rate", "Enter"); t.wait(1000); t.open_params()
    area = t.js("window.__MOCK.z[0].spr.area"); flow = t.js("window.__MOCK.z[0].spr.flow")
    check("zone: typing the rate sets the flow for the area", abs(flow * 60 / area - 100) < 0.5, (flow, area))
    # help sheet
    t.p.locator("#zpg details").nth(1).locator("button.row:not(.go)").last.click(); t.wait(400)
    check("zone: soil help sheet", t.sheet_open() and "cm" in t.txt("#shb"))
    t.close_sheet()
    # cycle and soak (the rate is now far above the infiltration)
    check("zone: cycle-and-soak note shown", t.p.locator("#zsk").count() == 1)
    t.p.locator("#zsk").click(); t.wait(400)
    check("zone: cycle-and-soak explanation", t.sheet_open())
    t.close_sheet()
    # pause window and pause-until sheets: the date and time fields line up and are wide enough
    for btn in ("#zday .btn.dn", "#znow .btn.dn"):
        t.p.locator(btn).click(); t.wait(500)
        g = t.js("Array.from(document.querySelectorAll('#shb input[type=date],#shb input[type=time]')).map(function(i){var r=i.getBoundingClientRect();return [i.type,Math.round(r.left),Math.round(r.width)]})")
        ds = [x for x in g if x[0] == "date"]; ts = [x for x in g if x[0] == "time"]
        check("zone: %s date fields line up" % btn, len(set(x[1] for x in ds)) == 1, g)
        check("zone: %s fields wide enough" % btn, all(x[2] >= 100 for x in ds) and all(x[2] >= 60 for x in ts), g)
        t.close_sheet()
    # minimum temperature stepper, covered switch
    c0 = t.js("window.__MOCK.z[0].cold")
    t.p.click("#zcp"); t.wait(1500)
    check("zone: minimum temperature +1", t.js("window.__MOCK.z[0].cold") == c0 + 1, (c0, t.js("window.__MOCK.z[0].cold")))
    t.p.locator("#zcov").click(); t.wait(1000)
    check("zone: covered switch", t.js("window.__MOCK.z[0].cov") == 1)
    t.p.locator("#zcov").click(); t.wait(600)
    check("zone: recent events listed", t.p.locator("#zhist .row").count() >= 1)
    t.shot("zone-phone")
    # another zone: placeholders, then its own values
    t.p.goto(APP + "#z2"); t.wait(60)
    t.wait(1500); t.open_params()
    check("zone: switching zones shows that zone's parameters", "Lawn from seed" in t.txt("#zpg") or "Custom" in t.txt("#zpg"), t.txt("#zpg")[:80])
    t.done("zone")

# ---------------------------------------------------------------- statistics
def test_stats(b):
    for vp, tag in ((PHONE, "phone"), (DESK, "desktop")):
        t = Page(b, vp=vp)
        t.go("#stats", 2500)
        check("stats %s: four zone filters" % tag, t.p.locator("#sz button").count() == 4)
        tops = t.js("Array.from(document.querySelectorAll('#sz button')).map(function(b){return Math.round(b.getBoundingClientRect().top)})")
        check("stats %s: the filters share one line" % tag, len(set(tops)) == 1, tops)
        check("stats %s: charts drawn" % tag, t.p.locator("#sc canvas").count() >= 3)
        y0 = t.js("__ST.ch[0]._Y(40)")
        bb = t.p.locator("#sc canvas").first.bounding_box()
        t.p.mouse.move(bb["x"] + bb["width"] / 2, bb["y"] + bb["height"] / 2)
        t.p.mouse.wheel(0, -900); t.wait(300)
        check("stats %s: zooming keeps the y scale" % tag, abs(t.js("__ST.ch[0]._Y(40)") - y0) < 0.5)
        for i in (1, 2, 0):
            t.p.locator("#sr button").nth(i).click(); t.wait(1500)
            check("stats %s: range %d draws" % (tag, i), t.p.locator("#sc canvas").count() >= 1)
        t.p.locator("#sz button").nth(1).click(); t.wait(400)
        check("stats %s: a zone filter toggles" % tag, t.p.locator("#sz button").nth(1).get_attribute("aria-pressed") == "false")
        t.p.locator("#srf").click(); t.wait(1500)
        check("stats %s: refresh redraws" % tag, t.p.locator("#sc canvas").count() >= 1)
        t.shot("stats-" + tag, full=False)
        t.done("stats " + tag)

# ---------------------------------------------------------------- history
def test_history(b):
    t = Page(b)
    t.go("#hist", 3000)
    check("history: rows loaded", t.p.locator("#hl .row").count() > 5)
    chips = t.p.locator("#hf button")
    names = chips.all_inner_texts()
    check("history: All, the zones, Cost filters", len(names) >= 6 and any("Cost" in x for x in names), names)
    chips.nth(1).click(); t.wait(500)
    zrows = t.p.locator("#hl .row").count()
    check("history: zone filter", zrows >= 1)
    t.js("window.__MOCK.log.length=0")
    t.p.goto(APP + "#set"); t.wait(500); t.p.goto(APP + "#hist"); t.wait(2500)
    t.p.locator("#hf button", has_text="Cost").click(); t.wait(2500)
    check("history: empty Cost list says why", "cost" in t.txt("#hl").lower(), t.txt("#hl")[:120])
    t.shot("history-phone")
    t.done("history")

# ---------------------------------------------------------------- health
def test_health(b):
    t = Page(b)
    t.go("#health", 2000)
    check("health: all OK", "working" in t.txt("#hp").lower() or "ok" in t.txt("#hp").lower(), t.txt("#hp"))
    check("health: one sensors card with soil rows", t.p.locator("#hsn .row").count() == 8)
    check("health: forecast rows", t.p.locator("#hwx .row").count() == 3)
    check("health: controller rows", t.p.locator("#hct .row").count() >= 6)
    for flag, word in (("tankLow", "tank"), ("flt", "safety"), ("envFault", "temperature"), ("rtcLost", "clock")):
        t.js("window.__MOCK.%s=1" % flag); t.wait(2600)
        check("health: problem shown for " + flag, word in t.txt("#hp").lower(), t.txt("#hp"))
        t.js("window.__MOCK.%s=0" % flag); t.wait(2600)
    # output test: start one valve, it reports running, stop it
    t.p.locator("#hot .btn").first.click(); t.wait(1500)
    check("health: output test starts valve 1", t.js("(window.__MOCK.tst||[]).some(function(x){return x.o=='valve'&&x.z==1})"))
    t.p.locator("#hot .btn").first.click(); t.wait(1200)
    check("health: output test stops", not t.js("(window.__MOCK.tst||[]).some(function(x){return x.o=='valve'&&x.z==1&&x.u*1000>Date.now()})"))
    # restart
    t.p.locator("#hrb button").click(); t.wait(400)
    check("health: restart asks first", t.sheet_open())
    t.p.locator("#shb .btn.del").click(); t.wait(600)
    check("health: restart confirmed", "Restart" in t.toast(), t.toast())
    t.shot("health-phone")
    t.done("health")

# ---------------------------------------------------------------- settings
def test_settings(b):
    t = Page(b)
    t.go("#set", 2000)
    secs = [x.split("\n")[0] for x in t.p.locator("#sl .lb").all_inner_texts()]
    check("settings: section order", secs[:3] == ["CONTROLLER", "APPEARANCE", "WATERING RULES"] and secs[-2:] == ["DATA", "ABOUT"], secs)
    # controller name: a bad hostname is refused
    t.p.fill("#s_name", "kert kert"); t.p.press("#s_name", "Enter"); t.wait(400)
    check("settings: invalid name refused", t.js("window.__MOCK.S.name") != "kert kert")
    # watering rule with a decimal comma and rounding note
    t.p.fill("#s_rainmm", "2,57"); t.p.press("#s_rainmm", "Enter"); t.wait(1200)
    check("settings: decimal comma accepted and rounded", abs(t.js("window.__MOCK.S.rainmm") - 2.6) < 1e-6, t.js("window.__MOCK.S.rainmm"))
    # live calibration readings
    r0 = t.txt("#s_raw0")
    t.js("window.__MOCK.z[0].soil=20"); t.wait(2600)
    check("settings: raw soil reading updates live", t.txt("#s_raw0") != r0, (r0, t.txt("#s_raw0")))
    check("settings: tank distance shown", "cm" in t.txt("#s_tnow"))
    # calibration order error
    t.p.fill("#s_cal0dry", "100"); t.p.press("#s_cal0dry", "Enter"); t.wait(500)
    check("settings: dry below wet refused", t.js("window.__MOCK.S.soilCal[0].dry") != 100)
    # cost: water price change asks keep or zero
    t.p.fill("#s_cpw", "700"); t.p.press("#s_cpw", "Enter"); t.wait(600)
    check("settings: price change asks how to continue", t.sheet_open())
    t.p.locator("#shb .row").first.click(); t.wait(1500)
    check("settings: price saved", t.js("window.__MOCK.S.cpw") == 700)
    # buzzer switch
    b0 = t.js("window.__MOCK.S.buzz")
    t.p.locator("#s_buzz").click(); t.wait(1000)
    check("settings: buzzer switch", t.js("window.__MOCK.S.buzz") != b0)
    t.p.locator("#s_buzz").click(); t.wait(600)
    # language and theme
    t.p.locator("#sl .seg button", has_text="HU").click(); t.wait(600)
    check("settings: language switch", "BEÁLLÍTÁSOK" in t.txt("#htl").upper() or "Beállítások" in t.txt("#htl"), t.txt("#htl"))
    t.p.locator("#sl .seg button", has_text="EN").click(); t.wait(600)
    t.p.locator("#sl .seg.sgf button").nth(2).click(); t.wait(300)
    check("settings: dark theme", t.js("document.documentElement.getAttribute('data-theme')") == "dark")
    t.p.locator("#sl .seg.sgf button").nth(0).click(); t.wait(300)
    # destructive actions: right-aligned in lists, centred alone; each asks first
    check("settings: factory reset is the last row", "factory" in t.p.locator("#sl .list").last.inner_text().lower())
    t.p.locator("#sl .list").last.locator("button").click(); t.wait(400)
    check("settings: factory reset asks first", t.sheet_open()); t.close_sheet()
    # mowing game: tap the firmware row 7 times
    fw = t.p.locator("#sl .row.id").last
    for _ in range(7): fw.click(); t.wait(40)
    t.wait(500)
    check("settings: mowing game opens", t.p.locator("#mg").count() == 1)
    t.p.locator("#mg .mgx").click(); t.wait(300)
    check("settings: mowing game closes", t.p.locator("#mg").count() == 0)
    t.shot("settings-phone")
    t.done("settings")

# ---------------------------------------------------------------- menu, winter mode, global DND
def test_menu(b):
    t = Page(b)
    t.go("", 1500)
    t.p.click("#hm"); t.wait(400)
    check("menu: opens", "on" in (t.p.get_attribute("#mn", "class") or ""))
    t.p.locator("#moff").click(); t.wait(1500)
    check("menu: winter mode on", t.js("window.__MOCK.g.off") == 1)
    t.p.locator("#moff").click(); t.wait(1500)
    check("menu: winter mode off", t.js("window.__MOCK.g.off") == 0)
    t.p.click("#mdnd"); t.wait(400)
    check("menu: global DND sheet", t.sheet_open())
    t.close_sheet()
    t.p.keyboard.press("Escape")
    # drag the sheet down by its grabber; the page must answer straight away
    t.p.click("#mdnd") if t.js("document.getElementById('mn').className=='on'") else (t.p.click("#hm"), t.wait(400), t.p.click("#mdnd"))
    t.wait(500)
    g = t.p.locator("#shg").bounding_box(); x, y = g["x"] + g["width"] / 2, g["y"] + g["height"] / 2
    t.p.mouse.move(x, y); t.p.mouse.down()
    for k in range(1, 11): t.p.mouse.move(x, y + 25 * k); t.wait(16)
    t.p.mouse.up(); t.wait(120)
    check("sheet: drag down closes", not t.sheet_open())
    check("sheet: grabber blocks browser panning", t.js("getComputedStyle(document.getElementById('shg')).touchAction") == "none")
    t.p.click("#hm", timeout=1000); t.wait(100)
    check("sheet: page answers right after a drag close", "on" in (t.p.get_attribute("#mn", "class") or ""))
    t.p.keyboard.press("Escape")
    for h, v in (("#stats", "v6"), ("#hist", "v3"), ("#health", "v4"), ("#set", "v5")):
        t.p.goto(APP + h); t.wait(600)
        check("menu: %s shows its page" % h, t.js("document.getElementById('%s').className.indexOf('on')>=0" % v))
    t.done("menu")

# ---------------------------------------------------------------- every page: both languages, both themes, both sizes
def test_matrix(b):
    pages = ["", "#z1", "#z2", "#stats", "#hist", "#health", "#set"]
    for lang in ("en", "hu"):
        for theme in ("light", "dark"):
            for vp, tag in ((PHONE, "phone"), (DESK, "desktop")):
                t = Page(b, lang, theme, vp)
                for h in pages:
                    t.go(h, 1800)
                    over = t.js("document.documentElement.scrollWidth>window.innerWidth+1")
                    check("matrix %s %s %s %s: no sideways scroll" % (lang, theme, tag, h or "#home"), not over)
                    cut = t.js("Array.from(document.querySelectorAll('.btn')).filter(function(b){var r=b.getBoundingClientRect(),c=b.closest('.list,.zc,.sb,.evs')||document.body,q=c.getBoundingClientRect();return r.width>0&&(r.right>q.right+1||r.left<q.left-1)}).map(function(b){return b.textContent.trim()})")
                    check("matrix %s %s %s %s: no button sticks out of its card" % (lang, theme, tag, h or "#home"), not cut, cut)
                    keys = t.js("Array.from(document.querySelectorAll('body *')).filter(function(e){return e.children.length==0&&/^[a-z]+_[a-z0-9_]+$/.test(e.textContent.trim())}).map(function(e){return e.textContent.trim()}).slice(0,5)")
                    check("matrix %s %s %s %s: no untranslated keys" % (lang, theme, tag, h or "#home"), not keys, keys)
                    if lang == "hu" and theme == "dark":
                        t.shot("%s-%s-%s" % ((h[1:] or "home"), tag, lang + "-" + theme))
                t.done("matrix %s %s %s" % (lang, theme, tag))

# ---------------------------------------------------------------- setup page (Wi-Fi portal)
def test_setup(b):
    SETUP = "http://10.73.0.1/"
    ctx = b.new_context(viewport=PHONE, locale="en-GB")
    p = ctx.new_page(); errs = []
    p.on("pageerror", lambda e: errs.append(str(e)))
    nets = {"w": 0, "f": 0, "v": "", "h": "irrigation", "a": "Irrigation-Setup-ABCD",
            "n": [{"s": "Home", "r": -48, "o": 0}, {"s": "Guest", "r": -70, "o": 1}], "j": {"s": "idle"}}
    # the controller as the page sees it: /status answers from a script the test sets
    ctl = {"status": [], "down": False, "done": 0, "saved": "", "home": False, "sta": False}
    page = (ROOT / "portal" / "index.html").read_text(encoding="utf-8")
    def js(r, o): r.fulfill(status=200, content_type="application/json", body=json.dumps(o))
    def serve(r):
        u = r.request.url.split("?")[0]
        if ctl["down"] and not u.endswith("/"): r.abort(); return
        if ctl["sta"]:                                     # station mode: only the app and /ping
            if u.endswith("/ping"): r.fulfill(status=200, body="ok")
            elif "?" in r.request.url: r.fulfill(status=200, content_type="text/html", body="<title>app</title>APP")
            elif u.endswith("/"): r.fulfill(status=200, content_type="text/html", body=page)   # the browser's stale copy
            else: r.fulfill(status=404, body="not found")
            return
        if u.endswith("/scan"): js(r, nets)
        elif u.endswith("/rescan"): r.fulfill(status=200, body="ok")
        elif u.endswith("/save"):
            ctl["saved"] = r.request.post_data; js(r, {"ok": 1})
        elif u.endswith("/status"):
            s = ctl["status"].pop(0) if len(ctl["status"]) > 1 else (ctl["status"][0] if ctl["status"] else {"s": "idle"})
            js(r, dict(s, h="irrigation"))
        elif u.endswith("/done"):
            ctl["done"] += 1; js(r, {"ok": 1})
        else: r.fulfill(status=200, content_type="text/html", body=page)
    def home(r):                                           # the controller on the home network
        if not ctl["home"]: r.abort(); return
        if r.request.url.endswith("/ping"): r.fulfill(status=200, headers={"Access-Control-Allow-Origin": "*"}, body="ok")
        else: r.fulfill(status=200, content_type="text/html", body="<title>app</title>APP")
    ctx.route("http://10.73.0.1/**", serve)
    ctx.route("http://192.168.0.23/**", home)
    ctx.route("http://irrigation.local/**", lambda r: r.abort())
    p.goto(SETUP); p.wait_for_timeout(800)
    check("setup: networks listed", p.locator("#nl .row").count() == 2)
    p.locator("#nl .row").first.click(); p.wait_for_timeout(500)
    check("setup: join view", p.input_value("#ssid") == "Home")
    p.fill("#pass", "short"); p.locator("#pass").blur(); p.wait_for_timeout(200)
    check("setup: short password refused", p.is_disabled("#ok"))
    p.click("#eye")
    check("setup: eye shows the password", p.get_attribute("#pass", "type") == "text")

    # wrong password: back to the form with the controller's answer, password kept for fixing
    ctl["status"] = [{"s": "conn", "v": "Home"}, {"s": "conn", "v": "Home"}, {"s": "fail", "v": "Home", "e": "pass"}]
    p.fill("#pass", "password0"); p.wait_for_timeout(200)
    p.click("#ok"); p.wait_for_timeout(500)
    check("setup: saved", "ssid=Home" in ctl["saved"], ctl)
    check("setup: connecting view", "v on" in p.get_attribute("#v3", "class") and "Home" in p.inner_text("#v3"))
    p.wait_for_timeout(3500)
    check("setup: wrong password reported", "did not accept the password" in p.inner_text("#er"), p.inner_text("#er"))
    check("setup: back on the form", "v on" in p.get_attribute("#v2", "class") and p.input_value("#pass") == "password0")

    # lost the setup network for a while: says so, then carries on
    ctl["status"] = [{"s": "conn", "v": "Home"}]
    p.fill("#pass", "password1"); p.wait_for_timeout(200)
    p.click("#ok"); p.wait_for_timeout(1200)
    ctl["down"] = True; p.wait_for_timeout(9500)
    check("setup: lost-connection note", p.is_visible("#lost") and "Irrigation-Setup-ABCD" in p.inner_text("#lost"))
    ctl["down"] = False; ctl["status"] = [{"s": "ok", "v": "Home", "ip": "192.168.0.23"}]; p.wait_for_timeout(2500)
    v4 = p.inner_text("#v4")
    check("setup: joined view, one button", "v on" in p.get_attribute("#v4", "class") and p.locator("#v4 .btn").count() == 1)
    check("setup: joined view says what Continue does", "switches off the Irrigation-Setup-ABCD network" in v4, v4)
    check("setup: joined view shows no address", "192.168" not in v4 and ".local" not in v4, v4)
    p.screenshot(path=str(SHOTS / "setup-joined.png"), full_page=True)

    # a reopened page goes straight to the result
    nets["j"] = {"s": "ok", "v": "Home", "ip": "192.168.0.23"}
    p.reload(); p.wait_for_timeout(1000)
    check("setup: reopened page shows the result", "v on" in p.get_attribute("#v4", "class") and ctl["done"] == 0)

    # Continue: setup network closes; the address, and the app opens once this device is home
    p.click("#fin"); p.wait_for_timeout(600)
    check("setup: continue sent", ctl["done"] == 1)
    check("setup: almost-done view", "v on" in p.get_attribute("#v5", "class") and "Join Home" in p.inner_text("#p5"))
    check("setup: the address is a link", p.inner_text("#a3") == "192.168.0.23" and p.get_attribute("#a3", "href") == "http://192.168.0.23/")
    check("setup: no tips at first", not p.is_visible("#sl"))
    p.wait_for_timeout(11000)
    check("setup: tips after 10 s", p.is_visible("#sl") and "irrigation.local" in p.inner_text("#sl"))
    p.screenshot(path=str(SHOTS / "setup-done.png"), full_page=True)
    ctl["home"] = True; p.wait_for_timeout(2000)
    check("setup: opens the app at the IP", p.url == "http://192.168.0.23/", p.url)

    # name.local opened through the setup network while joined: they want the app, so Continue
    ctl["done"] = 0; ctl["home"] = False
    ctx.unroute("http://irrigation.local/**"); ctx.route("http://irrigation.local/**", serve)
    p.goto("http://irrigation.local/"); p.wait_for_timeout(1200)
    check("setup: name.local while joined continues by itself", ctl["done"] == 1 and "v on" in p.get_attribute("#v5", "class"))

    # a stale copy of the setup page while the controller is online
    ctl["sta"] = True
    p.goto(SETUP); p.wait_for_timeout(1200)
    check("setup: says the controller is online", "v on" in p.get_attribute("#v0", "class") and "Already online" in p.inner_text("#v0"))
    p.click("#opn"); p.wait_for_timeout(600)
    check("setup: opens the app", "APP" in p.content())
    check("setup: no JavaScript errors", not errs, errs[:3])
    ctx.close()

    # Hungarian
    ctl["sta"] = False
    ctx = b.new_context(viewport=PHONE, locale="hu-HU"); p = ctx.new_page()
    nets["j"] = {"s": "ok", "v": "Otthon", "ip": "192.168.0.23"}
    ctx.route("http://10.73.0.1/**", serve); p.goto(SETUP); p.wait_for_timeout(1000)
    txt = p.inner_text("#v4")
    check("setup hu: joined texts", "Csatlakozva" in txt and "Tovább" in txt and "kikapcsolja az Irrigation-Setup-ABCD hálózatot" in txt, txt)
    p.screenshot(path=str(SHOTS / "setup-joined-hu.png"), full_page=True)
    p.click("#fin"); p.wait_for_timeout(600)
    txt = p.inner_text("#v5")
    check("setup hu: almost done", "Csatlakozz a(z) Otthon hálózatra, majd nyisd meg:" in txt and "magától fog csatlakozni" in txt, txt)
    check("setup hu: no 'beállító' anywhere", "beállító" not in p.content())
    ctx.close()

def main():
    t0 = time.time()
    with sync_playwright() as pw:
        b = pw.chromium.launch()
        tests = (test_home, test_zone, test_stats, test_history, test_health, test_settings, test_menu, test_setup, test_matrix)
        only = sys.argv[1:]                                 # e.g. run.py stats setup
        for f in tests:
            if only and f.__name__[5:] not in only:
                continue
            try:
                f(b)
            except Exception as e:                      # a broken selector is a failure, not a crash
                check(f.__name__ + " ran to the end", False, repr(e)[:300])
        b.close()
    print("%d checks, %d failed (%.0f s)" % (checks, fails, time.time() - t0))
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
