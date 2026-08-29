! Default cartridge header for SEGA Saturn, used when this runtime is assembled
! outside an export. The exporter overwrites this file with the scene's own details.
!
! The Saturn header is 64 bytes at offset 0x80 in the IP.BIN file.
! Fields include system name, region, maker code, serial number, etc.

                .ascii  "SEGA SATURN     "       ! System name (16 bytes)
                .ascii  "(C)HATCH 2026.08"       ! Copyright notice (16 bytes)
                .ascii  "HATCH SCENE                                     "
                .ascii  "                                                "
                .ascii  "GM 00000000-00  "       ! Serial number (14 bytes) + version (2)
                .ascii  "JUE             "       ! Region code (16 bytes)
                .long   0x00000000                ! Device information
                .long   0x00000000
                .long   0x00000000
                .long   0x00000000
                .ascii  "AKNETK          "       ! Maker name (16 bytes)
