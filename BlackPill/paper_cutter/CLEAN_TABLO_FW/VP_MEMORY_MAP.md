# VP / SP — CLEAN_TABLO

Полный экран: ЗАДАНО / ОСТАЛОСЬ / м/с + СБРОС / СТОП. Тап **ЗАДАНО** → стр. **10**.
Режим Run — автоматически при первых импульсах энкодера (вал запускают вручную).
Шестерёнка → стр. **16** (настройки). Тап строки → стр. **17** (клавиатура VarInput).

Фон страниц: **`32.icl`** (T5LCFG слот **0x20**).

**MCU:** `D:\paper_cutter\BlackPill\paper_cutter\CLEAN_TABLO_FW\`

**Resolution** 800×480, **SPADDRESS** 0x5000.

| Role | VP | Widget |
|------|-----|--------|
| Задано | 6000 | ArtTextShow + VarInput→10 |
| Осталось | 6010 | ArtTextShow |
| Скорость м/с | 6020 | ArtTextShow ×0.01 |
| СТОП / СБРОС | 6051 / 6052 | BitButton |
| Шестерёнка | 6055 | BitButton Pic_Next=16 |
| НАЗАД (настр.) | 6056 | BitButton Pic_Next=0 |
| KB buffer | 6080 | ArtTextShow |
| Keys / OK / Cancel | 60A1–60AD | (ASCII на стр. 10) |
| Расстояние торможения, м | 6090 | VarInput LONG →17 |
| Время тормоз (1), мс | 6094 | VarInput INT16 →17 |
| Время отпуск (0), мс | 6096 | VarInput INT16 →17 |
| Ограничение скорости ×0.01 | 6098 | VarInput INT16 →17 |
| Лимит скорости OFF / ON | 609A / 609B | BitButton |
