# VP / SP — CLEAN_TABLO

Полный экран: ЗАДАНО / ОСТАЛОСЬ / прогресс / м/с / об/мин + СТАРТ / СБРОС / СТОП. Тап **ЗАДАНО** → стр. **10**.

Ошибки (MCU → PIC_SET):
- **11** — реверс энкодера
- **12** — нет сигнала энкодера (Run без импульсов)
- **13** — СТАРТ при ЗАДАНО = 0
- **14** — скачок скорости
- **15** — обрыв канала A или B

Фон страниц: **`32.icl`** (T5LCFG слот **0x20**).

**MCU:** `D:\paper_cutter\BlackPill\paper_cutter\CLEAN_TABLO_FW\`

**Resolution** 800×480, **SPADDRESS** 0x5000.

| Role | VP | Widget |
|------|-----|--------|
| Задано | 6000 | ArtTextShow (long, целые м) |
| Осталось | 6010 | ArtTextShow (long, целые м) |
| Скорость м/с | 6020 | ArtTextShow ×0.01 |
| Обороты | 6024 | ArtTextShow |
| Прогресс 0…100 % | 6030 | **IconShow 0x5A00** → `26.icl` icons 70…170 |
| СТАРТ / СТОП / СБРОС | 6050 / 6051 / 6052 | BitButton |
| ИСПРАВИТЬ (стр. 11–15) | 6054 | BitButton — сброс любой ошибки |
| Open keypad | 6053 | BitButton Pic_Next=10 |
| KB buffer | 6080 | ArtTextShow |
| Keys / OK / Cancel | 60A1–60AD | BitButton |
