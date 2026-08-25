! Default cartridge header, used when this runtime is assembled outside an
! export. The exporter overwrites this file with the scene's own details.
!
! Every field is fixed width. The lengths below are the Mega Drive's header
! layout, which the 32X keeps: 16 for the system name, 16 for the copyright,
! 48 each for the domestic and international titles, 16 for the serial and
! checksum together, 16 for the supported input.
		.ascii	"SEGA 32X        "
		.ascii	"(C)HATCH 2026.08"
		.ascii	"HATCH SCENE                                     "
		.ascii	"HATCH SCENE                                     "
		.ascii	"GM 00000000-00  "
		.ascii	"J               "
		.long	0x000000
		.long	0x3FFFFF
		.long	0xFF0000
		.long	0xFFFFFF
		.long	0x20202020
		.long	0x20202020
		.long	0x20202020
		.ascii	"            "
		.ascii	"                                        "
		.ascii	"JUE             "
