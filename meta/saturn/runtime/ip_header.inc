! The Saturn disc header, as the console expects it byte for byte.
! The exporter rewrites this file with the scene's own title; this copy is the
! default, so the runtime still assembles on its own.

    .ascii  "SEGA SEGASATURN "              ! 0x00: what the console looks for
    .ascii  "SEGA TP T-000   "              ! 0x10: maker
    .ascii  "T-000     "                    ! 0x20: product number
    .ascii  "V1.000"                        ! 0x2A: version
    .ascii  "20260101"                      ! 0x30: release date
    .ascii  "CD-1/1  "                      ! 0x38: device information
    .ascii  "JTUBKAEL  "                    ! 0x40: areas it will run in
    .ascii  "      "                        ! 0x4A: spare
    .ascii  "J       "                      ! 0x50: peripherals -- a digital pad
    .ascii  "        "
    .ascii  "HATCH SCENE                     " ! 0x60: title, 112 bytes
    .ascii  "                                "
    .ascii  "                                "
    .ascii  "                "
    .space  16, 0                           ! 0xD0: reserved
    .long   0x00001000                      ! 0xE0: how much of this to load
    .long   0x00000000                      ! 0xE4: reserved
    .long   0x06002000                      ! 0xE8: master SH-2 stack
    .long   0x06001E00                      ! 0xEC: slave SH-2 stack
    .long   0x06004000                      ! 0xF0: where the first program goes
    .long   0x00000000                      ! 0xF4: and how big it is -- 0 is "all of it"
    .long   0x00000000                      ! 0xF8: reserved
    .long   0x00000000
