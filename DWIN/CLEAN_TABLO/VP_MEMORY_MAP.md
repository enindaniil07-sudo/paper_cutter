# VP / SP — CLEAN_TABLO

Полный экран: ЗАДАНО / ОСТАЛОСЬ / м/с + СБРОС / СТОП. Тап **ЗАДАНО** → стр. **10**.
Режим Run — автоматически при первых импульсах энкодера (вал запускают вручную).

Фон страниц: **`32.icl`** (T5LCFG слот **0x20**).

**MCU:** `D:\paper_cutter\BlackPill\paper_cutter\CLEAN_TABLO_FW\`

**Resolution** 800×480, **SPADDRESS** 0x5000.

| Role | VP | Widget |
|------|-----|--------|
| Задано | 6000 | ArtTextShow |
| Осталось | 6010 | ArtTextShow |
| Скорость м/с | 6020 | ArtTextShow ×0.01 |
| СТОП / СБРОС | 6051 / 6052 | BitButton |
| Open keypad | 6053 | BitButton Pic_Next=10 |
| KB buffer | 6080 | ArtTextShow |
| Keys / OK / Cancel | 60A1–60AD | BitButton |
