# CLEAN_TABLO — show / слои / прогресс

## Корневые причины (2026-07-23)

1. **Полный Python-rewrite `14ShowFile.bin`** ломает смену страниц (настройки «под» главным).
2. **DGUS Save→Generate** даёт рабочий контейнер, но:
   - VP 6000/6010 = **UINT16** → цифры 0/пусто при MCU U32
   - **нет** IconShow VP6030 → ползунок/прогресс мёртв
   - стр.16 без ArtText → только фон (это ок для слоёв)
3. **Python `ensure_brake()` (cnt=1 на page16)** снова ломал слои.
4. `BuildFromDesign` раньше затирал show шаблоном из TEST_PROJECT.

## Текущий безопасный патч

`scripts/form_clean_tablo_show_pages.py`:
- база: `_from_dgus_generate/14ShowFile.bin`
- LONG32 для 6000/6010
- вставка IconShow VP6030 в page0 (со сдвигом KB)
- **page16 остаётся пустой** (cnt=0)

## Если слои снова сломаются

Значит вставка IconShow тоже непереносима на этой панели. Тогда:
1. В DGUS на `00.bmp` добавьте IconShow VP6030 / 26.icl 70–170
2. Save→Generate
3. Скопируйте show в `_from_dgus_generate/`
4. Патч только типов (без вставки виджетов)

## MCU

`actSettingsOpen`: один `dwinSetPage(16)` после Pic_Next (без двойного вызова). Перепрошейте BlackPill.
