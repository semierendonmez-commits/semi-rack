# SEMI Family — VCV Rack + 4ms MetaModule

Plugin ailesinin Eurorack serisi. Tek kaynak iki hedefe derlenir: VCV Rack 2 (`plugin.so` / `.vcvplugin`) ve 4ms MetaModule (`SemiFamily.mmplugin`). Paneller Early Cyber ile klasik Eurorack arası bir dilde: koyu panel, çerçeveli OLED ekranlar, eteklikli knob'lar ve somunlu jack'ler.

| modül | HP | ne | motor |
|---|---|---|---|
| NEXUSCLIP | 10 | Clip-to-zero clipper: CLIP (7 eğri) / SPECTRAL / LIMIT, alttan doyurma | `engines/nexusclip` (nexusclipctz) |
| RETICULUM | 16 | 8 node'lu rete gürültü ağı, Lissajous + node matrisi, RAND/WALK/DRUNK modülasyon, artifex + Beads | `engines/reticulum` |
| SEEDWALK | 16 | 16 sabitlenmiş net üzerinde yürüyen Markov topu, glide'lı matris geçişi | `engines/seedwalk` |
| EPOCH | 12 | Uzun saat: enerji ve sessizlik planı; BUS kablosuyla aileyi yönetir | `engines/epoch` |
| TURNAROUND | 12 | 4/8/16/32 bar dönüşleri, edit bütçesi, roll, wall, landing gap | `engines/turnaround` |

Motorlar plugin repolarından değiştirilmeden kopyalandı. Değişen yerler her motorun `sources.mk` dosyasında yazıyor.

## Derleme

```sh
# VCV Rack (Rack SDK 2.6.x)
make RACK_DIR=/path/to/Rack-SDK -j8 && make dist RACK_DIR=/path/to/Rack-SDK

# MetaModule (arm-none-eabi-gcc 12.2/12.3/15.3 PATH'te)
cmake -S metamodule -B build/mm -G Ninja -DMETAMODULE_SDK_DIR=/path/to/metamodule-plugin-sdk
cmake --build build/mm            # -> dist/metamodule/SemiFamily.mmplugin

# Paneller (SVG + 240 px PNG + layout başlıkları), tek kaynak: tools/panels/*.py
pip install fonttools cairosvg && python3 tools/panelgen.py
```

## BUS kablosu (FamilyBus CV)

Dört kanallı tek bir poly kablo kullanılır, çünkü MetaModule poly kabloyu 4 sesle sınırlıyor. Kanallar: POS, ENERGY, SCENE, EVENT. Ayrıntılar `src/common/FamilyBusCV.hpp` dosyasında. Örnek zincir: EPOCH `BUS·4` → TURNAROUND `BUS·4` → (sonraki kardeş).

## Belgeler

- `docs/METAMODULE_DISPLAY.md`: MetaModule ekran kuralları (yenileme hızı, katman, kırpma, çizgi kalınlığı, bellek bütçesi)
- `docs/PORTING.md`: yeni bir plugin'i seriye taşımanın beş adımı
- `tests/`: motorların çevrimdışı testleri (plugin build'ine dahil değil)

GPL-3.0-or-later. Üçüncü parti kodlar (stmlib, Beads: MIT; artifex/forsitan: GPL-3.0) `engines/thirdparty` altında, kendi lisanslarıyla.
