# Klímaváltozásra felkészített, adatvezérelt öntözőrendszer

Négyzónás, ESP32-alapú öntözővezérlő, amely a saját érzékelői és az időjárás-előrejelzés alapján dönti el, hogy egy zóna kapjon-e vizet, és ha igen, mennyit. Telefonról kezelhető, telepítés nélkül, és hálózat nélkül is működik.

A repository a teljes projektet tartalmazza: a firmware-t, a webes felületet, a KiCad-terveket, a szimulációt, az előrejelzés-kutatást, valamint a TDK-dolgozatot és a hozzá tartozó dokumentumokat.

A projekt az Óbudai Egyetem Kandó Kálmán Villamosmérnöki Karán készült (2026 ősz).

**Szerzők:** [Nemes Dániel](https://github.com/CITROMOSEPER), [Nyul Balázs](https://github.com/snotor)
**Konzulens:** Sándor Tamás

> **Állapot:** a végleges firmware (v1.3.2) és a webes felület elkészült, a maketten lefutottak a funkcionális tesztek. A vízmegtakarítási számok szimulációból származnak, a makett vízfogyasztás-mérése nem készült el.

## Tartalom

| Mappa vagy fájl | Tartalom |
|---|---|
| [`Final PlatformIO code/Irrigation system/`](Final%20PlatformIO%20code/Irrigation%20system/) | **Végleges firmware (v1.3.2)**, webes felület, PC-s és böngészős tesztek, dokumentáció |
| [`Forecast research/`](Forecast%20research/) | Négy időjárási modell egyhónapos összevetése egy közeli állomás méréseivel |
| [`szimulacio/`](szimulacio/) | A döntési logika Python-szimulációja négy budapesti idényre |
| [`Áramkör/Öntözőberendezés/`](Áramkör/Öntözőberendezés/) | KiCad kapcsolási rajz és NYÁK-terv, biztonsági mentésekkel |
| [`Rendszertervek/`](Rendszertervek/) | Az eredeti logikai és fizikai rendszerterv (a koncepció korábbi állapota) |
| [`Arduino - Irrigation system - final/`](Arduino%20-%20Irrigation%20system%20-%20final/) | A firmware 1.0.0-s állapota (2026. október 31.) |
| [`Arduino - discontinued/`](Arduino%20-%20discontinued/) | Lezárt első firmware, USB-s kapcsolattal |
| [`Python App - discontinued/`](Python%20App%20-%20discontinued/) | Lezárt PC-s kezelőprogram az első firmware-hez |
| [`TDK_dolgozat_ND_NyB.docx`](TDK_dolgozat_ND_NyB.docx) | A TDK-dolgozat |
| [`TDK_tesztjegyzokonyv_ND_NyB.docx`](TDK_tesztjegyzokonyv_ND_NyB.docx) | Kitöltött tesztjegyzőkönyv a makett vizsgálatairól |
| [`TDK_szimulacios_jegyzokonyv_ND_NyB.docx`](TDK_szimulacios_jegyzokonyv_ND_NyB.docx) | Szimulációs jegyzőkönyv |
| [`TDK_MI_nyilatkozat_ND_NyB.docx`](TDK_MI_nyilatkozat_ND_NyB.docx) | Nyilatkozat a mesterséges intelligencia használatáról |
| [`TDK_bemutato_ND_NyB.docx`](TDK_bemutato_ND_NyB.docx) | A TDK-téma rövid bemutatója |
| [`Kreatív megoldások – prezentáció.pptx`](Kreatív%20megoldások%20–%20prezentáció.pptx) | A tárgyi előadás diái |

## Funkciók

- 4 független zóna, zónánként saját naptárral, növény-, talaj- és szórófej-beállítással
- 3 üzemmód: ütemezett, érzékelő alapú (talajszenzoros) és időjárás alapú (szenzor nélküli, FAO-56 vízmérleg)
- Számolt vízadag a talaj vízhiányából, a FAO-56 fejlődési szakaszaival (változó Kc és gyökérmélység)
- Másodperc pontosságú futásidő és szakaszos öntözés (cycle and soak), zónánkénti térfogatárammal
- Esőtiltás két modell (DWD ICON és ECMWF) egyezése alapján a következő 24 órára; internet nélkül Hargreaves-becslés a helyi hőmérsékletből
- Mobilos webes felület, amelyet maga az eszköz szolgál ki; statisztika, napló, kétnyelvű (magyar, angol), világos és sötét téma
- Biztonsági réteg: szelep és szivattyú sorrendje, tartályszint, futásidő, napi keret, watchdog
- Áramszünet után a megszakadt öntözés naplózása és egyszeri folytatása, a hibás órakiolvasások szűrése
- Tiltási időszakok, fagy- és hidegvédelem, téli mód, nyomógombos vezérlés, hangjelzések

## Hardver

| Egység | Típus |
|---|---|
| Vezérlő | ESP32-WROOM-32 fejlesztőlap |
| Talajnedvesség | HW-390, 4 db |
| Tartályszint | HC-SR04 |
| Eső | SA-28 |
| Hőmérséklet, páratartalom | BME280 |
| Valós idejű óra | DS1302 |
| Relék | G2RL-1, 5 db |
| Szelepek | SF-02-12V, 4 db |
| Szivattyú | AD5P-PUMP-H-BK |
| Táp | 12 V DC, 5 V-os kapcsolóüzemű átalakítóval |

A makett teljes anyagköltsége 43 553 Ft. A végleges lábkiosztás a [`PINS.h`](Final%20PlatformIO%20code/Irrigation%20system/include/PINS.h) fájlban található. A KiCad-terv egy korábbi firmware-hez készült, ezért a végleges lábkiosztástól több ponton eltér; az eltéréseket a dolgozat 7.6. alfejezete sorolja fel.

## Firmware fordítása és feltöltése

Ehhez [PlatformIO](https://platformio.org/) szükséges. A parancsokat a `Final PlatformIO code/Irrigation system` mappából kell futtatni:

```bash
pio run -t upload
```

```bash
pio device monitor
```

Első indításkor az eszköz `Irrigation-Setup-XXXX` nevű nyílt Wi-Fi-hálózatot hoz létre. A beállító oldal a `http://10.73.0.1/` címen érhető el, és a csatlakozás után kiírja a vezérlő IP-címét. Utána a felület az `irrigation.local` címen is elérhető.

A firmware részletes leírása a [firmware README-jében](Final%20PlatformIO%20code/Irrigation%20system/README.md), a változások a [CHANGELOG-ban](Final%20PlatformIO%20code/Irrigation%20system/CHANGELOG.md), az API a [`docs/API.md`](Final%20PlatformIO%20code/Irrigation%20system/docs/API.md) fájlban, a fejlesztés története a [`docs/TDK-coding-journey.md`](Final%20PlatformIO%20code/Irrigation%20system/docs/TDK-coding-journey.md) fájlban található.

## Webes felület kipróbálása

A [`portal/app-demo.html`](Final%20PlatformIO%20code/Irrigation%20system/portal/app-demo.html) fájlt letöltés után böngészőben megnyitva a felület hardver nélkül kipróbálható. A megjelenő adatok szimuláltak.

## Tesztek

A firmware PC-re is lefordítható, a hardvert csonkok helyettesítik. Az 1.3.2-es készlet 403 ellenőrzést tartalmaz, a futtatáshoz `g++` és POSIX-környezet kell (Linux, WSL vagy Git Bash). A parancsokat a firmware mappájából kell indítani:

```bash
git clone --depth 1 -b v7.4.2 https://github.com/bblanchon/ArduinoJson.git /tmp/aj
```

```bash
test/host/build.sh /tmp/aj/src
```

A webes felület böngészős tesztjei (321 ellenőrzés) Pythonnal és Playwrighttal futnak:

```bash
pip install playwright && playwright install chromium
```

```bash
test/ui/run.sh
```

A 2026. november 7-i kódellenőrzéskor mind a 403, mind a 321 ellenőrzés hibátlanul lefutott.

## Eredmények

### Makett (2026. 11. 06–07.)

- A tíz funkcionális tesztből kilenc megfelelt, egy részben (a visszacsatolásos adagnál a talaj nem száradt az alsó határ alá).
- Mért térfogatáram zónánként: 1,07 / 0,62 / 0,80 / 0,96 L/perc.
- A tartályszint-mérés eltérése a vonalzós értéktől: 0,05–0,24 cm.

### Előrejelzés-összevetés (2026. 10. 09. – 11. 07.)

- 27 kiértékelt nap, 49,1 mm eső, 6 csapadékos nap, referencia egy kb. 900 m-re lévő amatőr állomás.
- A hőmérsékletet az ECMWF és a MET Norway jelezte előre a legpontosabban (1,5 °C átlagos hiba), a GFS 2,5 °C-kal melegebbet jelzett.
- A következő 24 órára az ICON mind a 6 csapadékos napot előre jelezte; 5–7 napos távon a csapadék-előrejelzés gyakorlatilag zaj.

A kiértékelés újrafuttatható a `Forecast research` mappából:

```bash
python compare.py
```

### Szimuláció (2022–2025, Budapest)

A szkriptek Python 3-mal, külső csomag nélkül futnak, a `szimulacio` mappából:

```bash
python sim.py
```

Az eredmények számított, nem mért értékek:

| Összehasonlítás a fix időzítőhöz | Vízmegtakarítás |
|---|---|
| Csak esőtiltás | 21% |
| Számolt adag, pázsit | 50–52% |
| Számolt adag, zöldség | 34–35% |
| Számolt adag, mediterrán növény | 75–76% |

## Ismert korlátok

- A webes felületnek nincs hitelesítése, ezért a helyi hálózaton bárki elérheti.
- Az időjárási lekérés nem ellenőrzi a TLS-tanúsítványt.
- Áramlásmérő nincs: a liter a szivattyúidő és a kimért térfogatáram szorzata.
- A talajszenzorok közös, közelítő kalibrációval futnak, a százalék relatív nedvességindex.
- Nincs távoli firmware-frissítés (OTA), és a NYÁK a végleges firmware-hez módosítást igényel.

## Mesterséges intelligencia használata

A firmware, a webes felület és a Python-alkalmazás a Claude Opus 5.5 modell segítségével készült, a dolgozat szövegének nagy része, a szimuláció és a jegyzőkönyvek szintén. A kapcsolási rajzhoz és a nyomtatott áramkörhöz nem használtunk MI-eszközt. A részleteket a dolgozat 12.4. alfejezete és az MI-nyilatkozat tartalmazza.
