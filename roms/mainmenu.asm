MAINMENU__HANDLEKEY:
	LD	HL, MENU1
	
        CP	$26 	; A key
        JR	NZ, N1
        JP	MENU__CHOOSENEXT
N1:
        CP	$25     ; Q key
        JR	NZ, N2
        JP	MENU__CHOOSEPREV
N2:
        CP	$21     ; ENTER key
        JR	NZ, N3
        JP	MENU__ACTIVATE
N3:
	RET
        
MAINMENU__STATUSCHANGED:
	BIT	1, A                 	; Bit 1 is WIFI Connected flag
        JR	NZ, WIFISTATUSCHANGED
	BIT	3, A                    ; Bit 3 is SD Card connected flag.
        JR	NZ, SDCARDSTATUSCHANGED
	RET


WIFISTATUSCHANGED:
	RET

SDCARDSTATUSCHANGED:
	BIT 	3, (IX) ; See if SDCARD is connected.
        JR	NZ, SDCARDCONNECTED
	
        ; SD card disconnected. Update menu to disable entry

        LD	HL, MENU1
	PUSH	HL
        POP	IX
        SET	0, (IX+MENU_OFF_FIRST_ENTRY+3) ; 3 == second entry (each has 3 bytes)
        JP	MENU__UPDATESELECTION

SDCARDCONNECTED:
	; Activate menu entry for SDcard
        LD	HL, MENU1
	PUSH	HL
        POP	IX
        RES	0, (IX+MENU_OFF_FIRST_ENTRY+3) ; 3 == second entry (each has 3 bytes)
        JP	MENU__UPDATESELECTION

ENTRY1HANDLER:
        LD	A, STATE_WIFICONFIGAP
        JP	ENTERSTATE

ENTRY2HANDLER:
	LD	A, STATE_SDCARDMENU
        JP	ENTERSTATE

ENTRY3HANDLER:
        LD	A, STATE_WIFICONFIGSTA
        JP	ENTERSTATE
	RET

ENTRY4HANDLER:
        
	ENDLESS

ENTRY5HANDLER:
	LD	A, STATE_WIFIPASSWORD; TEST
        JP	ENTERSTATE
	RET
        
MAINMENU__SETUP:
       	LD	IX, MENU1
        LD	(IX + FRAME_OFF_WIDTH), 28 ; Menu width 24
        LD	(IX + FRAME_OFF_NUMBER_OF_LINES), 4 ; Menu visible entries
        LD	(IX + MENU_OFF_DATA_ENTRIES), 5 ; Menu actual entries 
        LD 	(IX + MENU_OFF_SELECTED_ENTRY), 0 ; Selected entry
        LD	(IX+FRAME_OFF_TITLEPTR), LOW(MENUTITLE)
        LD	(IX+FRAME_OFF_TITLEPTR+1), HIGH(MENUTITLE)
        ; Entry 1
        LD	(IX+MENU_OFF_FIRST_ENTRY), 0 ; Flags
        LD	(IX+MENU_OFF_FIRST_ENTRY+1), LOW(ENTRY1)
        LD	(IX+MENU_OFF_FIRST_ENTRY+2), HIGH(ENTRY1);

        LD	(IX+MENU_OFF_FIRST_ENTRY+3), 1 ; Flags
        LD	(IX+MENU_OFF_FIRST_ENTRY+4), LOW(ENTRY2)
        LD	(IX+MENU_OFF_FIRST_ENTRY+5), HIGH(ENTRY2);
        
        LD	(IX+MENU_OFF_FIRST_ENTRY+6), 0 ; Flags
        LD	(IX+MENU_OFF_FIRST_ENTRY+7), LOW(ENTRY3)
        LD	(IX+MENU_OFF_FIRST_ENTRY+8), HIGH(ENTRY3)
        
        LD	(IX+MENU_OFF_FIRST_ENTRY+9), 0 ; Flags
        LD	(IX+MENU_OFF_FIRST_ENTRY+10), LOW(ENTRY4)
        LD	(IX+MENU_OFF_FIRST_ENTRY+11), HIGH(ENTRY4)

        LD	(IX+MENU_OFF_FIRST_ENTRY+12), 0 ; Flags
        LD	(IX+MENU_OFF_FIRST_ENTRY+13), LOW(ENTRY5)
        LD	(IX+MENU_OFF_FIRST_ENTRY+14), HIGH(ENTRY5)

	LD	(IX+MENU_OFF_CALLBACKPTR), LOW(MENUCALLBACKTABLE)
        LD	(IX+MENU_OFF_CALLBACKPTR+1), HIGH(MENUCALLBACKTABLE)

        LD 	(IX+MENU_OFF_DISPLAY_OFFSET), 0
        LD 	(IX+MENU_OFF_SELECTED_ENTRY), 0
        RET

MENUCALLBACKTABLE:
	DEFW ENTRY1HANDLER
        DEFW ENTRY2HANDLER
        DEFW ENTRY3HANDLER
        DEFW ENTRY4HANDLER
        DEFW ENTRY5HANDLER

MENUTITLE:
	DB 	"ZX Interface Z", 0
ENTRY1: DB	"Setup WiFi Access Point", 0
ENTRY2: DB	"Load from SD Card", 0
ENTRY3: DB	"Connect to WiFi network", 0
ENTRY4: DB	"Goto BASIC", 0
ENTRY5: DB	"Hidden entry", 0
