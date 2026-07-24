# DWIN_SET — flash files (CLEAN_TABLO)

Background ICL slot **32** (`T5LCFG[8]=0x20`).

## Обязательный набор

| File | Role |
|------|------|
| `T5LCFG.CFG` | byte 0x08 = **32** |
| `13TouchFile.bin` | STOP/RESET/gear + VarInput ЗАДАНО + settings 17→18 |
| `14ShowFile.bin` | ArtText стр.0; page16–18 пустые (без Python ArtText) |
| `22_Config.bin` | нули |
| `32.icl` | фоны 0,1,2,10–18 |
| `00.bmp`…`18.bmp` | те же фоны BMP |
| `24/25/26.icl` | цифры / прогресс |
| `0_DWIN_ASC.HZK` | шрифт |

## Прошивка панели

1. Выключить питание панели полностью  
2. Вставить SD (`F:\` и `F:\DWIN_SET` одинаковые)  
3. Включить → **дождаться конца** загрузки  
4. Выключить → включить  

MCU: `CLEAN_TABLO_FW` (шестерёнка → 4 параметра настроек).

Build: `BuildFromDesign.ps1`
