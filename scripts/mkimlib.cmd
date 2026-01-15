if not exist %2 (
	lib /NOLOGO /MACHINE:%1 /DEF:imports.def /OUT:%2
)
