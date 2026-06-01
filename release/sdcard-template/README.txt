Retro-Go S31 SD card template

Format the SD card as FAT32, then extract this template to the root of the card.
Place your own ROMs, BIOS files and cover images in the folders below.

ROM folders:
- /roms/nes
- /roms/gb
- /roms/gbc
- /roms/gba
- /roms/gw
- /roms/sms
- /roms/gg
- /roms/sg
- /roms/coleco
- /roms/pce
- /roms/lynx
- /roms/snes
- /roms/md
- /roms/msx

Cover art:
- /romart/<system>/<game name>.png
- Example: /romart/nes/Super Mario Bros.png

BIOS files:
- /retro-go/bios/gb_bios.bin
- /retro-go/bios/gbc_bios.bin
- /retro-go/bios/fds_bios.bin
- /retro-go/bios/msx/

Notes:
- This template intentionally does not include ROMs, BIOS files or cover packs.
- Saves are written under /retro-go/saves.
- Audio on the ESP32-S31-Korvo-1 ES8389 path is still being debugged in this port.
