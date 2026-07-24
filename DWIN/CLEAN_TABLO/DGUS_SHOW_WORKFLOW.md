# CLEAN_TABLO — show / слои / прогресс

## Корневые причины

1. **Полный Python-rewrite `14ShowFile.bin`** ломает смену страниц (настройки «под» главным).
2. **ArtText / любые виджеты на page16** в Python-патче — снова ломают слои на этой панели.
3. **`dwinSetPage(16)` вместе с Pic_Next=16** — UART и тач дерутся за композитинг.
4. DGUS Save→Generate даёт рабочий контейнер; page16 без виджетов — слои ок.

## Текущий безопасный патч

`scripts/form_clean_tablo_show_pages.py`:
- база: `_from_dgus_generate/14ShowFile.bin`
- LONG32 для 6000/6010
- вставка IconShow VP6030 в page0
- **page16 остаётся пустой** (cnt=0) — жёсткое правило
- пустые слоты не указывают на MAIN (0x4000)

Ввод настроек: VarInput → стр. 17 (цифры в окне клавиатуры). Значения в окошках стр. 16 без ArtText пока не рисуются (иначе снова слои).

## MCU

`actSettingsOpen`: только `pushSettings()`, **без** `dwinSetPage(16)`.
