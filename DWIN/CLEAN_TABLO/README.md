# CLEAN_TABLO

Тап **ЗАДАНО** → Variable Data Input → стр. **10** → значение в **VP 6000**.

## Экраны в `DWIN_SET`

| Файл | Назначение |
|------|------------|
| `00.bmp` | главный |
| `01/02/03.bmp` | Pic_On СТАРТ / СБРОС / СТОП |
| `10.bmp` | клавиатура (KB для VarInput) |
| `24.icl` / `25.icl` | шрифты ArtText |
| `32.icl` | упакованные фоны для панели |

```powershell
.\BuildFromDesign.ps1
```

После правок в DGUS GUI: Variable Data Input на **VP 6000**, тип **long integer (4 byte / 0x01)**, **N_Int=5**, лимит **0…99999** (максимум для пяти цифр).
