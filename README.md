# Klímaváltozásra felkészített, adatvezérelt öntözőrendszer

Négyzónás, ESP32-alapú öntözővezérlő, amely a saját érzékelői és az időjárási adatok alapján dönti el, hogy egy zóna kapjon-e vizet, és ha igen, mennyit. Telefonról kezelhető, telepítés nélkül, és hálózat nélkül is működik.

A repository a teljes projektet tartalmazza: a firmware-t, a webes felületet, a KiCad-terveket, a szimulációt, valamint a TDK-dolgozatot és a hozzá tartozó dokumentumokat.

A projekt az Óbudai Egyetem Kandó Kálmán Villamosmérnöki Karán készült (2026 ősz). Szerzők: Nemes Dániel, Nyul Balázs. Konzulens: Sándor Tamás.

> **Állapot:** a végleges firmware (v1.0.0), a webes felület és a kapcsolási rajz elkészült. A makett mérései még folyamatban vannak, ezért az alábbi megtakarítási számok szimulációból származnak.

## Tartalom

| Mappa vagy fájl | Tartalom |
|---|---|
| [`Arduino - Irrigation system - final/`](Arduino%20-%20Irrigation%20system%20-%20final/) | Végleges firmware (PlatformIO), webes felület, PC-s tesztek |
| [`Áramkör/Öntözőberendezés/`](Áramkör/Öntözőberendezés/) | KiCad kapcsolási rajz és NYÁK-terv, biztonsági mentésekkel |
| [`rendszertervek/`](rendszertervek/) | Az eredeti logikai és fizikai rendszerterv (a koncepció korábbi állapota) |
| [`szimulacio/`](szimulacio/) | A firmware döntési logikájának Python-szimulációja, bemenő adatok és eredmények |
| [`Arduino - discontinued/`](Arduino%20-%20discontinued/) | Lezárt első firmware, USB-s kapcsolattal |
| [`Python App - discontinued/`](Python%20App%20-%20discontinued/) | Lezárt PC-s kezelőprogram az első firmware-hez |
| [`TDK_dolgozat_ND_NyB.docx`](TDK_dolgozat_ND_NyB.docx) | A TDK-dolgozat |
| [`TDK_tesztjegyzokonyv_ND_NyB.docx`](TDK_tesztjegyzokonyv_ND_NyB.docx) | Tesztjegyzőkönyv a makett vizsgálataihoz |
| [`TDK_szimulacios_jegyzokonyv_ND_NyB.docx`](TDK_szimulacios_jegyzokonyv_ND_NyB.docx) | Szimulációs jegyzőkönyv |
| [`TDK_MI_nyilatkozat_ND_NyB.docx`](TDK_MI_nyilatkozat_ND_NyB.docx) | Nyilatkozat a mesterséges intelligencia használatáról |
| [`TDK_bemutato_ND_NyB.docx`](TDK_bemutato_ND_NyB.docx) | A TDK-téma rövid bemutatója |

## Funkciók

- 4 független zóna, zónánként saját naptárral, növény-, talaj- és szórófej-beállítással
- 3 üzemmód: időzített, visszacsatolásos (talajszenzoros) és időjárás alapú (szenzor nélküli, FAO-56 vízmérleg)
- Számolt vízadag a talaj vízhiányából
- Open-Meteo előrejelzés API-kulcs nélkül, internet nélkül Hargreaves-becslés a helyi hőmérsékletből
- Mobilos webes felület, amelyet maga az eszköz szolgál ki; első indításkor beállító portál
- Biztonsági réteg: szelep és szivattyú sorrendje, tartályszint, futásidő, napi keret, watchdog
- Tiltási időszakok, fagy- és hidegvédelem, téli mód, kézi öntözés, nyomógombos vezérlés
- CSV-napló minden öntözésről és kihagyásról, költségmérő

## Hardver

| Egység | Típus |
|---|---|
| Vezérlő | ESP32-WROOM-32 fejlesztőlap |
| Talajnedvesség | HW-390, 4 db |
| Tartályszint | HC-SR04 |
| Eső | SA-28 |
| Hőmérséklet, páratartalom, légnyomás | BME280 |
| Valós idejű óra | DS1302 |
| Relék | G2RL-1, 5 db |
| Szelepek | SF-02-12V, 4 db |
| Szivattyú | AD5P-PUMP-H-BK |
| Táp | 12 V DC, 5 V-os kapcsolóüzemű átalakítóval |

A végleges lábkiosztás a [`PINS.h`](Arduino%20-%20Irrigation%20system%20-%20final/include/PINS.h) fájlban található. A KiCad-terv az első firmware-hez készült, ezért a végleges lábkiosztástól több ponton eltér. Az eltéréseket a dolgozat 7.6. alfejezete sorolja fel. A terv KiCad 9-cel nyitható meg.

## Webes felület kipróbálása

Az [`irrigation-app-demo - web app.html`](Arduino%20-%20Irrigation%20system%20-%20final/irrigation-app-demo%20-%20web%20app.html) fájlt letöltés után böngészőben megnyitva a felület hardver nélkül kipróbálható. A megjelenő adatok szimuláltak.

## Firmware fordítása és feltöltése

Ehhez [PlatformIO](https://platformio.org/) szükséges. A parancsokat az `Arduino - Irrigation system - final` mappából kell futtatni:

```bash
pio run -t upload
```

```bash
pio device monitor
```

Első indításkor az eszköz `Irrigation-Setup-XXXX` nevű nyílt Wi-Fi-hálózatot hoz létre, ahol megadható a saját hálózat. Utána a felület az `irrigation.local` címen érhető el.

A weboldalak a `platformio.ini` jelenlegi beállításával nem generálódnak újra automatikusan. A `portal/` mappa módosítása után a `tools/build_pages.py` szkriptet kézzel kell futtatni, vagy vissza kell kapcsolni az `extra_scripts` sort. A firmware részletes leírása a [firmware README-jében](Arduino%20-%20Irrigation%20system%20-%20final/README.md) van.

## Tesztek

A firmware PC-re is lefordítható, a hardvert csonkok helyettesítik. A készlet 128 ellenőrzést tartalmaz. A futtatáshoz `g++` és POSIX-környezet kell (Linux, WSL vagy Git Bash), a parancsokat a firmware mappájából kell indítani:

```bash
git clone --depth 1 -b v7.4.2 https://github.com/bblanchon/ArduinoJson.git /tmp/aj
```

```bash
test/host/build.sh /tmp/aj/src
```

A makett kézi vizsgálatainak lépéseit a tesztjegyzőkönyv tartalmazza.

## Szimuláció

A szkriptek Python 3-mal, külső csomag nélkül futnak. A bemenet a `wx.json`, az Open-Meteo 2022–2025-ös budapesti napi archív adata. A parancsokat a `szimulacio` mappából kell futtatni:

```bash
python sim.py
```

```bash
python band.py
```

```bash
python harg.py .
```

Az eredmények számított, nem mért értékek:

| Összehasonlítás a fix időzítőhöz | Vízmegtakarítás |
|---|---|
| Csak esőtiltás | 21% |
| Számolt adag, pázsit | 50–52% |
| Számolt adag, zöldség | 34–35% |
| Számolt adag, mediterrán növény | 75–76% |

A módszertan, a korlátok és az évenkénti adatok a szimulációs jegyzőkönyvben találhatók.

## A fejlesztés útja

Az első változat Arduino-firmware volt egy Python/tkinter kezelőprogrammal, USB soros kapcsolattal és OpenWeatherMap-adatokkal. A használat során kiderült, hogy a laptopos beállítás és a 16 paraméteres feltételrendszer nehézkes, ezért a rendszert Wi-Fi-s webes felületre, három üzemmódra és számolt vízadagra terveztük át. A végleges firmware új kódbázis. A lezárt változatok csak a fejlesztési folyamat dokumentálása miatt szerepelnek itt.

Az első kezelőprogram a `Python App - discontinued` mappából indítható, a `pyserial` csomag telepítése után:

```bash
python main.py
```

## Ismert korlátok

- A webes felületnek nincs hitelesítése, ezért a helyi hálózaton bárki elérheti.
- Az időjárási lekérés nem ellenőrzi a TLS-tanúsítványt.
- Áramlásmérő nincs: a liter a szivattyúidő és a kimért térfogatáram szorzata.
- Nincs távoli firmware-frissítés (OTA).
- A NYÁK a végleges firmware-hez módosítást igényel.

## Mesterséges intelligencia használata

A firmware, a webes felület és a Python-alkalmazás a Claude Opus 5.5 modell segítségével készült, a dolgozat szövegének nagy része, a szimuláció és a jegyzőkönyvek szintén. A kapcsolási rajzhoz és a nyomtatott áramkörhöz nem használtunk MI-eszközt. A részleteket a dolgozat 12.4. alfejezete és az MI-nyilatkozat tartalmazza.
