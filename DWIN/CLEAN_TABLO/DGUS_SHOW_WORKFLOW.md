# CLEAN_TABLO — show / слои

## Жёсткое правило

**Не создавать ArtText на стр. 16/17/18 с нуля в Python.**  
Idle-цифры настроек — только через **DGUS Save→Generate**, затем минимальный патч.

Пустые слоты → FF-sentinel (никогда не `0x4000` / MAIN).

## Рабочий патч (`form_clean_tablo_show_pages.py`)

База: `_from_dgus_generate/14ShowFile.bin`

1. IconShow VP6030 на стр.0 (сдвиг указателей, без ломания page17)  
2. Нормализация ArtText: LONG/UINT16, Icon0=30, цвет белый  
3. Если в базе на стр.17 уже есть ArtText — **сохранить**  
4. Стр. **16 / 18** — пустые на sentinel  

Скрипты: `form_clean_tablo_show_pages.py`, `gen_clean_tablo_touch_bin.py`, `render_clean_tablo_settings.py`.  
SD: `F:\` и `F:\DWIN_SET`; после смены show — полный цикл питания.
