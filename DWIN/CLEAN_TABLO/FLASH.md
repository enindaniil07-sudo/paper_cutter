# DWIN_SET — flash files (CLEAN_TABLO)

Background ICL slot **32** (`T5LCFG[8]=0x20`).

## Обязательный набор

| File | Role |
|------|------|
| `T5LCFG.CFG` | byte 0x08 = **32** |
| `13TouchFile.bin` | STOP/RESET/gear + VarInput ЗАДАНО + page16 brake |
| `14ShowFile.bin` | ArtText стр.0 + стр.16 VP6090 |
| `22_Config.bin` | нули |
| `32.icl` | фоны 0,1,2,10–16 |
| `00.bmp`…`16.bmp` | те же фоны BMP |
| `24/25/26.icl` | цифры / прогресс |
| `0_DWIN_ASC.HZK` | шрифт |

## Прошивка панели

1. Выключить питание панели полностью  
2. Вставить SD (`F:\` и `F:\DWIN_SET` одинаковые)  
3. Включить → **дождаться конца** загрузки  
4. Выключить → включить  

MCU: `CLEAN_TABLO_FW` (без СТАРТ; шестерёнка → расстояние торможения).

Build: `BuildFromDesign.ps1`
