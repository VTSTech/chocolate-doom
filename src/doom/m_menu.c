//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM selection menu, options, episode etc.
//	Sliders and icons. Kinda widget stuff.
//


#include <stdlib.h>
#include <ctype.h>


#include "doomdef.h"
#include "doomkeys.h"
#include "dstrings.h"

#include "d_main.h"
#include "deh_main.h"

#include "i_input.h"
#include "i_joystick.h"
#include "i_swap.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_misc.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "r_local.h"


#include "hu_stuff.h"

#include "g_game.h"

#include "m_argv.h"
#include "m_controls.h"
#include "p_saveg.h"
#include "p_setup.h"

#include "s_sound.h"

#include "doomstat.h"

// Data.
#include "sounds.h"

#include "m_menu.h"


//
// defaulted values
//
int			mouseSensitivity = 5;

// Show messages has default, 0 = off, 1 = on
int			showMessages = 1;
	

// Blocky mode, has default, 0 = high, 1 = normal
int			detailLevel = 0;
int			screenblocks = 9;

// temp for screenblocks (0-9)
int			screenSize;

// -1 = no quicksave slot picked!
int			quickSaveSlot;

 // 1 = message to be printed
int			messageToPrint;
// ...and here is the message string!
const char		*messageString;

// message x & y
int			messx;
int			messy;
int			messageLastMenuActive;

// timed message = no input from user
boolean			messageNeedsInput;

void    (*messageRoutine)(int response);

char gammamsg[5][26] =
{
    GAMMALVL0,
    GAMMALVL1,
    GAMMALVL2,
    GAMMALVL3,
    GAMMALVL4
};

// we are going to be entering a savegame string
int			saveStringEnter;              
int             	saveSlot;	// which slot to save in
int			saveCharIndex;	// which char we're editing
static boolean          joypadSave = false; // was the save action initiated by joypad?
// old save description before edit
char			saveOldString[SAVESTRINGSIZE];  

boolean			inhelpscreens;
boolean			menuactive;

#define SKULLXOFF		-32
#define LINEHEIGHT		16

char			savegamestrings[10][SAVESTRINGSIZE];

char	endstring[160];

static boolean opldev;

//
// MENU TYPEDEFS
//
typedef struct
{
    // 0 = no cursor here, 1 = ok, 2 = arrows ok
    short	status;
    
    char	name[10];
    
    // choice = menu item #.
    // if status = 2,
    //   choice=0:leftarrow,1:rightarrow
    void	(*routine)(int choice);
    
    // hotkey in menu
    char	alphaKey;			
} menuitem_t;



typedef struct menu_s
{
    short		numitems;	// # of menu items
    struct menu_s*	prevMenu;	// previous menu
    menuitem_t*		menuitems;	// menu items
    void		(*routine)();	// draw routine
    short		x;
    short		y;		// x,y of menu
    short		lastOn;		// last item user was on in menu
} menu_t;

short		itemOn;			// menu item skull is on
short		skullAnimCounter;	// skull animation counter
short		whichSkull;		// which skull to draw

// graphic name of skulls
// warning: initializer-string for array of chars is too long
const char *skullName[2] = {"M_SKULL1","M_SKULL2"};

// current menudef
menu_t*	currentMenu;                          

//
// PROTOTYPES
//
static void M_NewGame(int choice);
static void M_Episode(int choice);
static void M_ChooseSkill(int choice);
static void M_LoadGame(int choice);
static void M_SaveGame(int choice);
static void M_Options(int choice);
static void M_EndGame(int choice);
static void M_ReadThis(int choice);
static void M_ReadThis2(int choice);
static void M_QuitDOOM(int choice);

static void M_ChangeMessages(int choice);
static void M_ChangeSensitivity(int choice);
static void M_ChangeFOV(int choice);		// [CDVK] FOV
static void M_Advanced(int choice);		// [CDVK] ADVANCED submenu
static void M_Shaders(int choice);		// [CDVK] SHADERS browser
static void M_ShadersPick(int choice);		// [CDVK] browser rows
static void M_ShadersNone(int choice);
static void M_DrawShadersBrowser(void);
static void M_SfxVol(int choice);
static void M_MusicVol(int choice);
static void M_SizeDisplay(int choice);
static void M_Sound(int choice);

static void M_FinishReadThis(int choice);
static void M_LoadSelect(int choice);
static void M_SaveSelect(int choice);
static void M_ReadSaveStrings(void);
static void M_QuickSave(void);
static void M_QuickLoad(void);

static void M_DrawMainMenu(void);
static void M_DrawReadThis1(void);
static void M_DrawReadThis2(void);
static void M_DrawNewGame(void);
static void M_DrawEpisode(void);
static void M_DrawOptions(void);
static void M_DrawAdvanced(void);	// [CDVK] ADVANCED submenu
static void M_DrawSound(void);
static void M_DrawLoad(void);
static void M_DrawSave(void);

static void M_DrawSaveLoadBorder(int x,int y);
static void M_SetupNextMenu(menu_t *menudef);
static void M_DrawThermo(int x,int y,int thermWidth,int thermDot);
static void M_WriteText(int x, int y, const char *string);
static int  M_TextScaledWidth(const char *string, patch_t *refpatch);  // [CDVK] ADVANCED
static void M_DrawTextScaled(int x, int y, const char *string, patch_t *refpatch);    // [CDVK] FOV
static int M_TextScaledWidth1x(const char *string);   // [CDVK] browser rows
static void M_DrawTextScaled1x(int x, int y, const char *string);     // [CDVK] browser rows
static int  M_StringWidth(const char *string);
static int  M_StringHeight(const char *string);
static void M_StartMessage(const char *string, void *routine, boolean input);
static void M_ClearMenus (void);




//
// DOOM MENU
//
enum
{
    newgame = 0,
    options,
    loadgame,
    savegame,
    readthis,
    quitdoom,
    main_end
} main_e;

menuitem_t MainMenu[]=
{
    {1,"M_NGAME",M_NewGame,'n'},
    {1,"M_OPTION",M_Options,'o'},
    {1,"M_LOADG",M_LoadGame,'l'},
    {1,"M_SAVEG",M_SaveGame,'s'},
    // Another hickup with Special edition.
    {1,"M_RDTHIS",M_ReadThis,'r'},
    {1,"M_QUITG",M_QuitDOOM,'q'}
};

menu_t  MainDef =
{
    main_end,
    NULL,
    MainMenu,
    M_DrawMainMenu,
    97,64,
    0
};


//
// EPISODE SELECT
//
enum
{
    ep1,
    ep2,
    ep3,
    ep4,
    ep_end
} episodes_e;

menuitem_t EpisodeMenu[]=
{
    {1,"M_EPI1", M_Episode,'k'},
    {1,"M_EPI2", M_Episode,'t'},
    {1,"M_EPI3", M_Episode,'i'},
    {1,"M_EPI4", M_Episode,'t'}
};

menu_t  EpiDef =
{
    ep_end,		// # of menu items
    &MainDef,		// previous menu
    EpisodeMenu,	// menuitem_t ->
    M_DrawEpisode,	// drawing routine ->
    48,63,              // x,y
    ep1			// lastOn
};

//
// NEW GAME
//
enum
{
    killthings,
    toorough,
    hurtme,
    violence,
    nightmare,
    newg_end
} newgame_e;

menuitem_t NewGameMenu[]=
{
    {1,"M_JKILL",	M_ChooseSkill, 'i'},
    {1,"M_ROUGH",	M_ChooseSkill, 'h'},
    {1,"M_HURT",	M_ChooseSkill, 'h'},
    {1,"M_ULTRA",	M_ChooseSkill, 'u'},
    {1,"M_NMARE",	M_ChooseSkill, 'n'}
};

menu_t  NewDef =
{
    newg_end,		// # of menu items
    &EpiDef,		// previous menu
    NewGameMenu,	// menuitem_t ->
    M_DrawNewGame,	// drawing routine ->
    48,63,              // x,y
    hurtme		// lastOn
};



//
// OPTIONS MENU
//
enum
{
    advanced,		// [CDVK] was endgame: ADVANCED submenu
    messages,
    shaders,		// [CDVK] was detail (Graphic Detail)
    scrnsize,
    option_empty1,
    mousesens,
    option_empty2,
    soundvol,
    opt_end
} options_e;

menuitem_t OptionsMenu[]=
{
    {1,"",		M_Advanced,'a'},	// [CDVK] ADVANCED (was End Game)
    {1,"M_MESSG",	M_ChangeMessages,'m'},
    {1,"",		M_Shaders,'h'},	// [CDVK] SHADERS browser (was Graphic Detail)
    {2,"M_SCRNSZ",	M_SizeDisplay,'s'},
    {-1,"",0,'\0'},
    {2,"M_MSENS",	M_ChangeSensitivity,'m'},
    {-1,"",0,'\0'},
    {1,"M_SVOL",	M_Sound,'s'}
};

menu_t  OptionsDef =
{
    opt_end,
    &MainDef,
    OptionsMenu,
    M_DrawOptions,
    60,37,
    0
};

//
// [CDVK] ADVANCED submenu (occupies the old End Game row): the FOV
// slider moved here from the Options menu.
//
enum
{
    adv_fov,
    adv_empty1,
    adv_end
} advanced_e;

menuitem_t AdvancedMenu[]=
{
    {2,"",		M_ChangeFOV,'f'},	// [CDVK] FOV
    {-1,"",0,'\0'}
};

menu_t  AdvancedDef =
{
    adv_end,
    &OptionsDef,
    AdvancedMenu,
    M_DrawAdvanced,
    60,37,
    0
};

//
// Read This! MENU 1 & 2
//
enum
{
    rdthsempty1,
    read1_end
} read_e;

menuitem_t ReadMenu1[] =
{
    {1,"",M_ReadThis2,0}
};

menu_t  ReadDef1 =
{
    read1_end,
    &MainDef,
    ReadMenu1,
    M_DrawReadThis1,
    280,185,
    0
};

enum
{
    rdthsempty2,
    read2_end
} read_e2;

menuitem_t ReadMenu2[]=
{
    {1,"",M_FinishReadThis,0}
};

menu_t  ReadDef2 =
{
    read2_end,
    &ReadDef1,
    ReadMenu2,
    M_DrawReadThis2,
    330,175,
    0
};

//
// SOUND VOLUME MENU
//
enum
{
    sfx_vol,
    sfx_empty1,
    music_vol,
    sfx_empty2,
    sound_end
} sound_e;

menuitem_t SoundMenu[]=
{
    {2,"M_SFXVOL",M_SfxVol,'s'},
    {-1,"",0,'\0'},
    {2,"M_MUSVOL",M_MusicVol,'m'},
    {-1,"",0,'\0'}
};

menu_t  SoundDef =
{
    sound_end,
    &OptionsDef,
    SoundMenu,
    M_DrawSound,
    80,64,
    0
};

//
// LOAD GAME MENU
//
enum
{
    load1,
    load2,
    load3,
    load4,
    load5,
    load6,
    load_end
} load_e;

menuitem_t LoadMenu[]=
{
    {1,"", M_LoadSelect,'1'},
    {1,"", M_LoadSelect,'2'},
    {1,"", M_LoadSelect,'3'},
    {1,"", M_LoadSelect,'4'},
    {1,"", M_LoadSelect,'5'},
    {1,"", M_LoadSelect,'6'}
};

menu_t  LoadDef =
{
    load_end,
    &MainDef,
    LoadMenu,
    M_DrawLoad,
    80,54,
    0
};

//
// SAVE GAME MENU
//
menuitem_t SaveMenu[]=
{
    {1,"", M_SaveSelect,'1'},
    {1,"", M_SaveSelect,'2'},
    {1,"", M_SaveSelect,'3'},
    {1,"", M_SaveSelect,'4'},
    {1,"", M_SaveSelect,'5'},
    {1,"", M_SaveSelect,'6'}
};

menu_t  SaveDef =
{
    load_end,
    &MainDef,
    SaveMenu,
    M_DrawSave,
    80,54,
    0
};


//
// M_ReadSaveStrings
//  read the strings from the savegame files
//
void M_ReadSaveStrings(void)
{
    FILE   *handle;
    int     i;
    char    name[256];

    for (i = 0;i < load_end;i++)
    {
        int retval;
        M_StringCopy(name, P_SaveGameFile(i), sizeof(name));

	handle = M_fopen(name, "rb");
        if (handle == NULL)
        {
            M_StringCopy(savegamestrings[i], EMPTYSTRING, SAVESTRINGSIZE);
            LoadMenu[i].status = 0;
            continue;
        }
        retval = fread(&savegamestrings[i], 1, SAVESTRINGSIZE, handle);
	fclose(handle);
        LoadMenu[i].status = retval == SAVESTRINGSIZE;
    }
}


//
// M_LoadGame & Cie.
//
void M_DrawLoad(void)
{
    int             i;
	
    V_DrawPatchDirect(72, 28, 
                      W_CacheLumpName(DEH_String("M_LOADG"), PU_CACHE));

    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
}



//
// Draw border for the savegame description
//
void M_DrawSaveLoadBorder(int x,int y)
{
    int             i;
	
    V_DrawPatchDirect(x - 8, y + 7,
                      W_CacheLumpName(DEH_String("M_LSLEFT"), PU_CACHE));
	
    for (i = 0;i < 24;i++)
    {
	V_DrawPatchDirect(x, y + 7,
                          W_CacheLumpName(DEH_String("M_LSCNTR"), PU_CACHE));
	x += 8;
    }

    V_DrawPatchDirect(x, y + 7, 
                      W_CacheLumpName(DEH_String("M_LSRGHT"), PU_CACHE));
}



//
// User wants to load this game
//
void M_LoadSelect(int choice)
{
    char    name[256];
	
    M_StringCopy(name, P_SaveGameFile(choice), sizeof(name));

    G_LoadGame (name);
    M_ClearMenus ();
}

//
// Selected from DOOM menu
//
void M_LoadGame (int choice)
{
    if (netgame)
    {
	M_StartMessage(DEH_String(LOADNET),NULL,false);
	return;
    }
	
    M_SetupNextMenu(&LoadDef);
    M_ReadSaveStrings();
}


//
//  M_SaveGame & Cie.
//
void M_DrawSave(void)
{
    int             i;
	
    V_DrawPatchDirect(72, 28, W_CacheLumpName(DEH_String("M_SAVEG"), PU_CACHE));
    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
	
    if (saveStringEnter)
    {
	i = M_StringWidth(savegamestrings[saveSlot]);
	M_WriteText(LoadDef.x + i,LoadDef.y+LINEHEIGHT*saveSlot,"_");
    }
}

//
// M_Responder calls this when user is finished
//
void M_DoSave(int slot)
{
    G_SaveGame (slot,savegamestrings[slot]);
    M_ClearMenus ();

    // PICK QUICKSAVE SLOT YET?
    if (quickSaveSlot == -2)
	quickSaveSlot = slot;
}

//
// Generate a default save slot name when the user saves to
// an empty slot via the joypad.
//
static void SetDefaultSaveName(int slot)
{
    // map from IWAD or PWAD?
    if (W_IsIWADLump(maplumpinfo) && strcmp(savegamedir, ""))
    {
        M_snprintf(savegamestrings[itemOn], SAVESTRINGSIZE,
                   "%s", maplumpinfo->name);
    }
    else
    {
        char *wadname = M_StringDuplicate(W_WadNameForLump(maplumpinfo));
        char *ext = strrchr(wadname, '.');

        if (ext != NULL)
        {
            *ext = '\0';
        }

        M_snprintf(savegamestrings[itemOn], SAVESTRINGSIZE,
                   "%s (%s)", maplumpinfo->name,
                   wadname);
        free(wadname);
    }
    M_ForceUppercase(savegamestrings[itemOn]);
    joypadSave = false;
}

//
// User wants to save. Start string input for M_Responder
//
void M_SaveSelect(int choice)
{
    int x, y;

    // we are going to be intercepting all chars
    saveStringEnter = 1;

    // We need to turn on text input:
    x = LoadDef.x - 11;
    y = LoadDef.y + choice * LINEHEIGHT - 4;
    I_StartTextInput(x, y, x + 8 + 24 * 8 + 8, y + LINEHEIGHT - 2);

    saveSlot = choice;
    M_StringCopy(saveOldString,savegamestrings[choice], SAVESTRINGSIZE);
    if (!strcmp(savegamestrings[choice], EMPTYSTRING))
    {
        savegamestrings[choice][0] = 0;

        if (joypadSave)
        {
            SetDefaultSaveName(choice);
        }
    }
    saveCharIndex = strlen(savegamestrings[choice]);
}

//
// Selected from DOOM menu
//
void M_SaveGame (int choice)
{
    if (!usergame)
    {
	M_StartMessage(DEH_String(SAVEDEAD),NULL,false);
	return;
    }
	
    if (gamestate != GS_LEVEL)
	return;
	
    M_SetupNextMenu(&SaveDef);
    M_ReadSaveStrings();
}



//
//      M_QuickSave
//
static char tempstring[90];

void M_QuickSaveResponse(int key)
{
    if (key == key_menu_confirm)
    {
	M_DoSave(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}

void M_QuickSave(void)
{
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }

    if (gamestate != GS_LEVEL)
	return;
	
    if (quickSaveSlot < 0)
    {
	M_StartControlPanel();
	M_ReadSaveStrings();
	M_SetupNextMenu(&SaveDef);
	quickSaveSlot = -2;	// means to pick a slot now
	return;
    }
    DEH_snprintf(tempstring, sizeof(tempstring),
                 QSPROMPT, savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring, M_QuickSaveResponse, true);
}



//
// M_QuickLoad
//
void M_QuickLoadResponse(int key)
{
    if (key == key_menu_confirm)
    {
	M_LoadSelect(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}


void M_QuickLoad(void)
{
    if (netgame)
    {
	M_StartMessage(DEH_String(QLOADNET),NULL,false);
	return;
    }
	
    if (quickSaveSlot < 0)
    {
	M_StartMessage(DEH_String(QSAVESPOT),NULL,false);
	return;
    }
    DEH_snprintf(tempstring, sizeof(tempstring),
                 QLPROMPT, savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring, M_QuickLoadResponse, true);
}




//
// Read This Menus
// Had a "quick hack to fix romero bug"
//
void M_DrawReadThis1(void)
{
    inhelpscreens = true;

    V_DrawPatchDirect(0, 0, W_CacheLumpName(DEH_String("HELP2"), PU_CACHE));
}



//
// Read This Menus - optional second page.
//
void M_DrawReadThis2(void)
{
    inhelpscreens = true;

    // We only ever draw the second page if this is 
    // gameversion == exe_doom_1_9 and gamemode == registered

    V_DrawPatchDirect(0, 0, W_CacheLumpName(DEH_String("HELP1"), PU_CACHE));
}

void M_DrawReadThisCommercial(void)
{
    inhelpscreens = true;

    V_DrawPatchDirect(0, 0, W_CacheLumpName(DEH_String("HELP"), PU_CACHE));
}


//
// Change Sfx & Music volumes
//
void M_DrawSound(void)
{
    V_DrawPatchDirect (60, 38, W_CacheLumpName(DEH_String("M_SVOL"), PU_CACHE));

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(sfx_vol+1),
		 16,sfxVolume);

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(music_vol+1),
		 16,musicVolume);
}

void M_Sound(int choice)
{
    M_SetupNextMenu(&SoundDef);
}

void M_SfxVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (sfxVolume)
	    sfxVolume--;
	break;
      case 1:
	if (sfxVolume < 15)
	    sfxVolume++;
	break;
    }
	
    S_SetSfxVolume(sfxVolume * 8);
}

void M_MusicVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (musicVolume)
	    musicVolume--;
	break;
      case 1:
	if (musicVolume < 15)
	    musicVolume++;
	break;
    }
	
    S_SetMusicVolume(musicVolume * 8);
}




//
// M_DrawMainMenu
//
void M_DrawMainMenu(void)
{
    V_DrawPatchDirect(94, 2,
                      W_CacheLumpName(DEH_String("M_DOOM"), PU_CACHE));
}




//
// M_NewGame
//
void M_DrawNewGame(void)
{
    V_DrawPatchDirect(96, 14, W_CacheLumpName(DEH_String("M_NEWG"), PU_CACHE));
    V_DrawPatchDirect(54, 38, W_CacheLumpName(DEH_String("M_SKILL"), PU_CACHE));
}

void M_NewGame(int choice)
{
    if (netgame && !demoplayback)
    {
	M_StartMessage(DEH_String(NEWGAME),NULL,false);
	return;
    }
	
    // Chex Quest disabled the episode select screen, as did Doom II.

    if (gamemode == commercial || gameversion == exe_chex)
	M_SetupNextMenu(&NewDef);
    else
	M_SetupNextMenu(&EpiDef);
}


//
//      M_Episode
//
int     epi;

void M_DrawEpisode(void)
{
    V_DrawPatchDirect(54, 38, W_CacheLumpName(DEH_String("M_EPISOD"), PU_CACHE));
}

void M_VerifyNightmare(int key)
{
    if (key != key_menu_confirm)
	return;
		
    G_DeferedInitNew(nightmare,epi+1,1);
    M_ClearMenus ();
}

void M_ChooseSkill(int choice)
{
    if (choice == nightmare)
    {
	M_StartMessage(DEH_String(NIGHTMARE),M_VerifyNightmare,true);
	return;
    }
	
    G_DeferedInitNew(choice,epi+1,1);
    M_ClearMenus ();
}

void M_Episode(int choice)
{
    if ( (gamemode == shareware)
	 && choice)
    {
	M_StartMessage(DEH_String(SWSTRING),NULL,false);
	M_SetupNextMenu(&ReadDef1);
	return;
    }

    epi = choice;
    M_SetupNextMenu(&NewDef);
}



//
// M_Options
//
static const char *msgNames[2] = {"M_MSGOFF","M_MSGON"};

void M_DrawOptions(void)
{
    V_DrawPatchDirect(108, 15, W_CacheLumpName(DEH_String("M_OPTTTL"),
                                               PU_CACHE));

    V_DrawPatchDirect(OptionsDef.x + 120, OptionsDef.y + LINEHEIGHT * messages,
                      W_CacheLumpName(DEH_String(msgNames[showMessages]),
                                      PU_CACHE));

    M_DrawThermo(OptionsDef.x, OptionsDef.y + LINEHEIGHT * (mousesens + 1),
                 10, mouseSensitivity);

    M_DrawThermo(OptionsDef.x,OptionsDef.y+LINEHEIGHT*(scrnsize+1),
                 9,screenSize);

    // [CDVK] ADVANCED row (replaces End Game) and SHADERS row
    // (placeholder): no label patches ship with the game WADs, so
    // M_DrawTextScaled draws them with the HU font, uniformly scaled to
    // the ink height of real menu patches (M_MSGOFF) so the rows match
    // their neighbours exactly.
    M_DrawTextScaled(OptionsDef.x, OptionsDef.y+LINEHEIGHT*advanced,
                     "ADVANCED...",
                     W_CacheLumpName(DEH_String("M_MSGOFF"), PU_CACHE));
    M_DrawTextScaled(OptionsDef.x, OptionsDef.y+LINEHEIGHT*shaders,
                     "SHADERS",
                     W_CacheLumpName(DEH_String("M_MSGOFF"), PU_CACHE));
}
void M_Options(int choice)
{
    M_SetupNextMenu(&OptionsDef);
}

//
//
// [CDVK] M_DrawAdvanced - ADVANCED submenu: the FOV row moved here
// from the Options menu.  The title is drawn in place of the M_OPTTTL
// "OPTIONS" patch at the same ink height, centered on the screen; the
// FOV label and value use the M_MSGOFF item patch as their size
// reference, and the thermo sits on the empty row below.
//
void M_DrawAdvanced(void)
{
    char    fovvalue[8];
    patch_t *title;
    patch_t *label;
    int     w;

    title = W_CacheLumpName(DEH_String("M_OPTTTL"), PU_CACHE);
    label = W_CacheLumpName(DEH_String("M_MSGOFF"), PU_CACHE);

    w = M_TextScaledWidth("ADVANCED", title);
    M_DrawTextScaled((SCREENWIDTH - w) / 2, 15, "ADVANCED", title);

    M_DrawTextScaled(AdvancedDef.x, AdvancedDef.y+LINEHEIGHT*adv_fov,
                     "FOV", label);
    M_snprintf(fovvalue, sizeof(fovvalue), "%d", fov);
    M_DrawTextScaled(AdvancedDef.x + 175, AdvancedDef.y+LINEHEIGHT*adv_fov,
                     fovvalue, label);
    M_DrawThermo(AdvancedDef.x, AdvancedDef.y+LINEHEIGHT*(adv_fov+1),
                 17,(fov-FOV_MIN)/4);
}
// [CDVK] Open the ADVANCED submenu (the old End Game row).
//
void M_Advanced(int choice)
{
    choice = 0;
    M_SetupNextMenu(&AdvancedDef);
}

//
// [CDVK] SHADERS browser (occupies the old SHADERS placeholder row in
// the Options menu). Shows every .slangp preset found by
// I_ShaderPresets() - the active preset's own folder, shaders/ beside
// the executable, shaders/ in the config dir - as a sliding window:
// the rows are always a contiguous slice of the full list, up/down
// moves through all of it (the window follows the cursor), left/right
// skips a windowful, and a pick is applied with I_SelectShaderPreset():
// a live hot-swap when the Vulkan path is up (prepare-then-commit
// inside i_video_vk.c; a broken preset leaves the current shader on
// screen), or a persist-for-next-run selection on the classic renderer.
// There is deliberately no "shaders off" row: that removal was its own
// fix.
//
#define SHADERS_PAGE_ROWS 8    // visible preset rows (the sliding window)

static menuitem_t ShadersMenu[SHADERS_PAGE_ROWS + 2];

static menu_t ShadersDef =
{
    0,                 // numitems, rebuilt on every open/scroll
    &OptionsDef,
    ShadersMenu,
    M_DrawShadersBrowser,
    60,48,
    0
};

static const char **shaders_list = NULL;    // scanned presets (i_video.c)
static int shaders_count = 0;
static int shaders_top = 0;                 // first visible preset

// Fill ShadersMenu[] for the window at shaders_top: one row per
// visible preset and the status -1 terminator. Rows are status 1
// (enter fires the routine; left/right are taken over by
// M_ShadersScroll for window skips).
static void M_BuildShaderPage(void)
{
    int rows = shaders_count - shaders_top;
    int i;

    if (rows > SHADERS_PAGE_ROWS)
    {
        rows = SHADERS_PAGE_ROWS;
    }
    if (rows < 0)
    {
        rows = 0;
    }
    for (i = 0; i < rows; i++)
    {
        ShadersMenu[i].status = 1;
        ShadersMenu[i].name[0] = '\0';
        ShadersMenu[i].routine = M_ShadersPick;
        ShadersMenu[i].alphaKey = '\0';
    }
    if (rows == 0)
    {
        // Empty folder: one informational row, selectable so the cursor
        // has somewhere to live, but it only shrugs.
        ShadersMenu[0].status = 1;
        ShadersMenu[0].routine = M_ShadersNone;
        rows = 1;
    }
    ShadersMenu[rows].status = -1;
    ShadersMenu[rows].routine = NULL;
    ShadersDef.numitems = (short) rows;
}

// Move the selection to absolute preset index sel, scrolling the
// window and rebuilding the rows so it stays visible.
static void M_ShadersShow(int sel)
{
    if (sel < 0)
    {
        sel = 0;
    }
    if (sel > shaders_count - 1)
    {
        sel = shaders_count - 1;
    }
    if (sel < shaders_top)
    {
        shaders_top = sel;
    }
    else if (sel >= shaders_top + SHADERS_PAGE_ROWS)
    {
        shaders_top = sel - SHADERS_PAGE_ROWS + 1;
    }
    itemOn = (short) (sel - shaders_top);
    ShadersDef.lastOn = itemOn;
    M_BuildShaderPage();
}

// The browser takes over the movement keys before the generic wrap:
// up/down walk the whole preset list (the window follows the cursor,
// so scrolling back up works), left/right skip a windowful in either
// direction. Every other key (enter, backspace, escape) falls through
// to the generic handling below.
static boolean M_ShadersScroll(int key)
{
    int sel;

    if (shaders_count == 0)
    {
        return false;    // empty-folder row: generic handling
    }
    sel = shaders_top + itemOn;
    if (sel > shaders_count - 1)
    {
        sel = shaders_count - 1;
    }
    if (key == key_menu_down)
    {
        if (sel + 1 > shaders_count - 1)
        {
            sel = 0;
        }
        else
        {
            sel = sel + 1;
        }
    }
    else if (key == key_menu_up)
    {
        if (sel - 1 < 0)
        {
            sel = shaders_count - 1;
        }
        else
        {
            sel = sel - 1;
        }
    }
    else if (key == key_menu_left)
    {
        if (sel - SHADERS_PAGE_ROWS < 0)
        {
            sel = 0;
        }
        else
        {
            sel = sel - SHADERS_PAGE_ROWS;
        }
    }
    else if (key == key_menu_right)
    {
        if (sel + SHADERS_PAGE_ROWS > shaders_count - 1)
        {
            sel = shaders_count - 1;
        }
        else
        {
            sel = sel + SHADERS_PAGE_ROWS;
        }
    }
    else
    {
        return false;
    }
    M_ShadersShow(sel);
    S_StartSound(NULL, sfx_pstop);
    return true;
}

//
// Open the browser from the Options menu's SHADERS row. The scan runs
// here so presets dropped into a shaders/ folder mid-session appear
// without a restart, and the window opens on the active preset.
//
static void M_Shaders(int choice)
{
    const char *cur;
    int i;
    int sel;

    choice = 0;
    shaders_list = I_ShaderPresets(&shaders_count);
    shaders_top = 0;
    sel = 0;
    cur = I_CurrentShaderPreset();
    for (i = 0; i < shaders_count; i++)
    {
        if (strcmp(shaders_list[i], cur) == 0)
        {
            sel = i;
            break;
        }
    }
    shaders_top = sel - sel % SHADERS_PAGE_ROWS;
    M_BuildShaderPage();
    M_SetupNextMenu(&ShadersDef);
    // M_SetupNextMenu resets itemOn from lastOn; place the cursor on
    // the active preset afterwards.
    itemOn = (short) (sel - shaders_top);
    if (itemOn >= ShadersDef.numitems)
    {
        itemOn = 0;
    }
    ShadersDef.lastOn = itemOn;
}

// Enter on a preset row: apply it.
static void M_ShadersPick(int choice)
{
    int idx = shaders_top + itemOn;

    choice = 0;
    if (itemOn >= ShadersDef.numitems || idx >= shaders_count)
    {
        return;
    }
    if (I_SelectShaderPreset(shaders_list[idx]))
    {
        // Live swap; the stats overlay already flashed the preset name.
    }
    else if (I_VulkanActive())
    {
        // A live swap that failed: the overlay flash says why.
        S_StartSound(NULL, sfx_oof);
    }
    else
    {
        players[consoleplayer].message =
            DEH_String("SHADER SAVED - RESTART TO APPLY");
    }
}

// Enter on the empty-folder row.
static void M_ShadersNone(int choice)
{
    choice = 0;
    S_StartSound(NULL, sfx_oof);
}

//
// [CDVK] Browser draw: title in place of the OPTIONS patch (same ink
// height as M_DrawAdvanced), then this window's rows in the HU font at
// its natural size - scaled to the M_MSGOFF item height, a single row
// reads as a title. The active preset carries a " *" marker; the
// animated skull (drawn generically by M_Drawer) is the cursor. Rows
// that would overflow the 320px screen are trimmed to "..".
//
static void M_DrawShadersBrowser(void)
{
    patch_t *title;
    const char *cur = I_CurrentShaderPreset();
    int rows = shaders_count - shaders_top;
    int maxw = SCREENWIDTH - ShadersDef.x - 12;
    int i;

    title = W_CacheLumpName(DEH_String("M_OPTTTL"), PU_CACHE);

    i = M_TextScaledWidth("SHADERS", title);
    M_DrawTextScaled((SCREENWIDTH - i) / 2, 15, "SHADERS", title);

    if (rows < 0)
    {
        rows = 0;
    }
    if (rows > SHADERS_PAGE_ROWS)
    {
        rows = SHADERS_PAGE_ROWS;
    }
    if (shaders_count == 0)
    {
        M_DrawTextScaled1x(ShadersDef.x, ShadersDef.y,
                           "(NO SHADERS FOUND)");
        M_DrawTextScaled1x(ShadersDef.x, ShadersDef.y + LINEHEIGHT,
                           "PUT .SLANGP FILES IN SHADERS/");
        return;
    }
    for (i = 0; i < rows; i++)
    {
        char disp[64];
        char *dot;
        size_t len;

        M_snprintf(disp, sizeof(disp), "%s",
                   M_BaseName(shaders_list[shaders_top + i]));
        dot = strrchr(disp, '.');
        if (dot != NULL)
        {
            *dot = '\0';
        }
        len = strlen(disp);
        while (len > 2)
        {
            char trybuf[68];
            M_snprintf(trybuf, sizeof(trybuf), "%s..", disp);
            if (M_TextScaledWidth1x(trybuf) <= maxw)
            {
                break;
            }
            disp[--len] = '\0';
        }
        if (len > 2)
        {
            M_snprintf(disp + strlen(disp), sizeof(disp) - strlen(disp),
                       "..");
        }
        if (strcmp(shaders_list[shaders_top + i], cur) == 0)
        {
            M_snprintf(disp + strlen(disp), sizeof(disp) - strlen(disp),
                       " *");
        }
        M_DrawTextScaled1x(ShadersDef.x, ShadersDef.y + LINEHEIGHT * i,
                           disp);
    }
}

//
//      Toggle messages on/off
//
void M_ChangeMessages(int choice)
{
    // warning: unused parameter `int choice'
    choice = 0;
    showMessages = 1 - showMessages;
	
    if (!showMessages)
	players[consoleplayer].message = DEH_String(MSGOFF);
    else
	players[consoleplayer].message = DEH_String(MSGON);

    message_dontfuckwithme = true;
}


//
// M_EndGame
//
void M_EndGameResponse(int key)
{
    if (key != key_menu_confirm)
	return;
		
    currentMenu->lastOn = itemOn;
    M_ClearMenus ();
    D_StartTitle ();
}

void M_EndGame(int choice)
{
    choice = 0;
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }
	
    if (netgame)
    {
	M_StartMessage(DEH_String(NETEND),NULL,false);
	return;
    }
	
    M_StartMessage(DEH_String(ENDGAME),M_EndGameResponse,true);
}




//
// M_ReadThis
//
void M_ReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef1);
}

void M_ReadThis2(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef2);
}

void M_FinishReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&MainDef);
}




//
// M_QuitDOOM
//
int     quitsounds[8] =
{
    sfx_pldeth,
    sfx_dmpain,
    sfx_popain,
    sfx_slop,
    sfx_telept,
    sfx_posit1,
    sfx_posit3,
    sfx_sgtatk
};

int     quitsounds2[8] =
{
    sfx_vilact,
    sfx_getpow,
    sfx_boscub,
    sfx_slop,
    sfx_skeswg,
    sfx_kntdth,
    sfx_bspact,
    sfx_sgtatk
};



void M_QuitResponse(int key)
{
    if (key != key_menu_confirm)
	return;
    if (!netgame)
    {
	if (gamemode == commercial)
	    S_StartSound(NULL,quitsounds2[(gametic>>2)&7]);
	else
	    S_StartSound(NULL,quitsounds[(gametic>>2)&7]);
	I_WaitVBL(105);
    }
    I_Quit ();
}


static const char *M_SelectEndMessage(void)
{
    const char **endmsg;

    if (logical_gamemission == doom)
    {
        // Doom 1

        endmsg = doom1_endmsg;
    }
    else
    {
        // Doom 2
        
        endmsg = doom2_endmsg;
    }

    return endmsg[gametic % NUM_QUITMESSAGES];
}


void M_QuitDOOM(int choice)
{
    DEH_snprintf(endstring, sizeof(endstring), "%s\n\n" DOSY,
                 DEH_String(M_SelectEndMessage()));

    M_StartMessage(endstring,M_QuitResponse,true);
}




void M_ChangeSensitivity(int choice)
{
    switch(choice)
    {
      case 0:
	if (mouseSensitivity)
	    mouseSensitivity--;
	break;
      case 1:
	if (mouseSensitivity < 9)
	    mouseSensitivity++;
	break;
    }
}

//
// [CDVK] FOV slider
//
void M_ChangeFOV(int choice)
{
    switch(choice)
    {
      case 0:
	if (fov > FOV_MIN)
	{
	    fov -= 2;
	    if (fov < FOV_MIN)		// hand-edited config value
		fov = FOV_MIN;
	}
	break;
      case 1:
	if (fov < FOV_MAX)
	{
	    fov += 2;
	    if (fov > FOV_MAX)		// hand-edited config value
		fov = FOV_MAX;
	}
	break;
    }

    R_SetViewSize (screenblocks, detailLevel);
}




void M_SizeDisplay(int choice)
{
    switch(choice)
    {
      case 0:
	if (screenSize > 0)
	{
	    screenblocks--;
	    screenSize--;
	}
	break;
      case 1:
	if (screenSize < 8)
	{
	    screenblocks++;
	    screenSize++;
	}
	break;
    }
	

    R_SetViewSize (screenblocks, detailLevel);
}




//
//      Menu Functions
//
void
M_DrawThermo
( int	x,
  int	y,
  int	thermWidth,
  int	thermDot )
{
    int		xx;
    int		i;

    xx = x;
    V_DrawPatchDirect(xx, y, W_CacheLumpName(DEH_String("M_THERML"), PU_CACHE));
    xx += 8;
    for (i=0;i<thermWidth;i++)
    {
	V_DrawPatchDirect(xx, y, W_CacheLumpName(DEH_String("M_THERMM"), PU_CACHE));
	xx += 8;
    }
    V_DrawPatchDirect(xx, y, W_CacheLumpName(DEH_String("M_THERMR"), PU_CACHE));

    V_DrawPatchDirect((x + 8) + thermDot * 8, y,
		      W_CacheLumpName(DEH_String("M_THERMO"), PU_CACHE));
}


void
M_StartMessage
( const char	*string,
  void*		routine,
  boolean	input )
{
    messageLastMenuActive = menuactive;
    messageToPrint = 1;
    messageString = string;
    messageRoutine = routine;
    messageNeedsInput = input;
    menuactive = true;
    return;
}


//
// Find string width from hu_font chars
//
int M_StringWidth(const char *string)
{
    size_t             i;
    int             w = 0;
    int             c;
	
    for (i = 0;i < strlen(string);i++)
    {
	c = toupper(string[i]) - HU_FONTSTART;
	if (c < 0 || c >= HU_FONTSIZE)
	    w += 4;
	else
	    w += SHORT (hu_font[c]->width);
    }
		
    return w;
}



//
//      Find string height from hu_font chars
//
int M_StringHeight(const char* string)
{
    size_t             i;
    int             h;
    int             height = SHORT(hu_font[0]->height);
	
    h = height;
    for (i = 0;i < strlen(string);i++)
	if (string[i] == '\n')
	    h += height;
		
    return h;
}


//
//      Write a string using the hu_font
//
void
M_WriteText
( int		x,
  int		y,
  const char *string)
{
    int		w;
    const char *ch;
    int		c;
    int		cx;
    int		cy;
		

    ch = string;
    cx = x;
    cy = y;
	
    while(1)
    {
	c = *ch++;
	if (!c)
	    break;
	if (c == '\n')
	{
	    cx = x;
	    cy += 12;
	    continue;
	}
		
	c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c>= HU_FONTSIZE)
	{
	    cx += 4;
	    continue;
	}
		
	w = SHORT (hu_font[c]->width);
	if (cx+w > SCREENWIDTH)
	    break;
	V_DrawPatchDirect(cx, cy, hu_font[c]);
	cx+=w;
    }
}

//
//
// [CDVK] M_DrawTextScaled - draw hu_font text sized to the M_* menu
// patches.  No menu patches exist for the ADVANCED / SHADERS / FOV
// labels, so they are drawn with the HU font (same red ramp as the
// stock menu font), scaled uniformly so the caps' visible ink height
// matches the ink of a reference patch: the ratio
// ref_ink_height / cap_ink_height is applied to every glyph, which
// preserves glyph shapes (a period stays a dot, not a stretched
// block) and keeps the labels exactly as tall as the neighbouring
// patch text.  Each glyph's ink is bottom-aligned to the cap
// baseline, so punctuation sits on the baseline.  Glyph pixels are
// read exactly like V_DrawPatch does; the result is written straight
// into I_VideoBuffer with bounds checks.
//

// Extent of the opaque pixels of a patch: the lowest topdelta and the
// highest topdelta+length over all posts in all columns.
static void M_PatchInkExtent(patch_t *patch, int *top, int *bottom)
{
    int c;
    int min_td = 0;
    int max_end = 0;
    int have = 0;

    for (c = 0; c < SHORT(patch->width); c++)
    {
        column_t *column;
        column_t *post;

        column = (column_t *)((byte *)patch + LONG(patch->columnofs[c]));
        post = column;
        while (post->topdelta != 0xff)
        {
            if (!have || post->topdelta < min_td)
            {
                min_td = post->topdelta;
            }
            if (!have || post->topdelta + post->length > max_end)
            {
                max_end = post->topdelta + post->length;
            }
            have = 1;
            post = (column_t *)((byte *)post + post->length + 4);
        }
    }

    if (!have)
    {
        min_td = 0;
        max_end = SHORT(patch->height);
    }

    *top = min_td;
    *bottom = max_end;
}

// Ink extent of the HU font's 'A' (the caps reference), measured once.
static void M_HUCapInk(int *top, int *bottom)
{
    static int cap_top = -1;
    static int cap_bottom;
    patch_t *cap;

    if (cap_top < 0)
    {
        cap = hu_font['A' - HU_FONTSTART];
        if (cap != NULL)
        {
            M_PatchInkExtent(cap, &cap_top, &cap_bottom);
        }
        if (cap == NULL || cap_bottom <= cap_top)
        {
            cap_top = 0;
            cap_bottom = 8;
        }
    }

    *top = cap_top;
    *bottom = cap_bottom;
}

// Uniform scale ratio for M_DrawTextScaled, as num/den: the ink
// height of refpatch over the cap ink height of the HU font (the 'A'
// glyph; every capital and digit shares it).  Measured rather than
// hardcoded, so modded fonts stay consistent.
static void M_TextScaledRatio(patch_t *refpatch, int *num, int *den)
{
    int cap_top, cap_bottom;
    int ref_top, ref_bottom;

    M_HUCapInk(&cap_top, &cap_bottom);
    M_PatchInkExtent(refpatch, &ref_top, &ref_bottom);

    *num = ref_bottom - ref_top;
    *den = cap_bottom - cap_top;

    if (*num < 1)
    {
        *num = 1;
    }
    if (*den < 1)
    {
        *den = 1;
    }
}

// Width of `string` at ratio num/den, for centering and trimming.
static int M_TextScaledWidthNum(const char *string, int num, int den)
{
    const char  *ch;
    int         c;
    int         w = 0;

    for (ch = string; *ch; ch++)
    {
        c = toupper(*ch) - HU_FONTSTART;
        if (c < 0 || c >= HU_FONTSIZE || hu_font[c] == NULL)
        {
            w += 4;
            continue;
        }
        w += (SHORT(hu_font[c]->width) * num + den / 2) / den + 1;
    }

    return w;
}

// Width of `string` as M_DrawTextScaled draws it, for centering.
static int M_TextScaledWidth(const char *string, patch_t *refpatch)
{
    int num, den;

    if (refpatch == NULL)
    {
        return 0;
    }

    M_TextScaledRatio(refpatch, &num, &den);

    return M_TextScaledWidthNum(string, num, den);
}

// Width of `string` as M_DrawTextScaled1x draws it.
static int M_TextScaledWidth1x(const char *string)
{
    return M_TextScaledWidthNum(string, 1, 1);
}

// The scaled draw itself: hu_font glyphs at ratio num/den, each
// glyph's ink bottom-aligned to a baseline at y + ink_top + num (the
// scaled cap ink height is exactly num).  Glyph pixels are read
// exactly like V_DrawPatch does; the result is written straight into
// I_VideoBuffer with bounds checks.
static void M_DrawTextScaledNum(int x, int y, const char *string,
                                int num, int den, int ink_top)
{
    const char  *ch;
    int         c;
    int         cx;
    int         baseline;
    int         gy;
    int         dx, dy;
    int         sx, sy;
    int         sw, sh, dw, dh;
    int         px;
    int         g_top, g_bottom;
    int         row_min = SCREENHEIGHT;
    int         row_max = -1;
    patch_t     *glyph;
    column_t    *column;
    column_t    *post;

    // The HU font draws every glyph from the same top line (caps are
    // ink-aligned to it), so scaling the glyph boxes uniformly and
    // bottom-aligning each glyph's ink to the cap baseline keeps the
    // letters aligned with each other and puts punctuation on the
    // baseline.
    baseline = y + ink_top + num;

    cx = x;

    for (ch = string; *ch; )
    {
        c = toupper(*ch++) - HU_FONTSTART;
        if (c < 0 || c >= HU_FONTSIZE || hu_font[c] == NULL)
        {
            cx += 4;            // like M_WriteText
            continue;
        }

        glyph = hu_font[c];
        sw = SHORT(glyph->width);
        sh = SHORT(glyph->height);
        dw = (sw*num + den/2) / den;
        dh = (sh*num + den/2) / den;
        if (dw < 1)
            dw = 1;
        if (dh < 1)
            dh = 1;

        M_PatchInkExtent(glyph, &g_top, &g_bottom);
        gy = baseline - ((g_bottom*num + den/2) / den);

        for (dx = 0; dx < dw; dx++)
        {
            sx = dx*sw/dw;
            column = (column_t *)((byte *)glyph + LONG(glyph->columnofs[sx]));

            for (dy = 0; dy < dh; dy++)
            {
                sy = dy*sh/dh;

                // step through the posts in the column (as V_DrawPatch)
                // and find the pixel at row sy of column sx.
                px = -1;
                post = column;
                while (post->topdelta != 0xff)
                {
                    if (sy >= post->topdelta
                        && sy < post->topdelta + post->length)
                    {
                        px = ((byte *)post + 3)[sy - post->topdelta];
                        break;
                    }
                    post = (column_t *)((byte *)post + post->length + 4);
                }
                if (px < 0)
                    continue;

                if (cx+dx >= 0 && cx+dx < SCREENWIDTH
                    && gy+dy >= 0 && gy+dy < SCREENHEIGHT)
                {
                    I_VideoBuffer[(gy+dy)*SCREENWIDTH + cx+dx] = (pixel_t)px;
                    if (gy+dy < row_min)
                        row_min = gy+dy;
                    if (gy+dy > row_max)
                        row_max = gy+dy;
                }
            }
        }
        cx += dw + 1;
    }

    if (row_max >= 0)
    {
        V_MarkRect(x, row_min, cx > x ? cx - x : 1, row_max - row_min + 1);
    }
}

static void
M_DrawTextScaled
( int           x,
  int           y,
  const char    *string,
  patch_t       *refpatch )
{
    int num, den;
    int ref_top, ref_bottom;

    if (refpatch == NULL)
    {
        return;
    }

    M_TextScaledRatio(refpatch, &num, &den);
    M_PatchInkExtent(refpatch, &ref_top, &ref_bottom);

    M_DrawTextScaledNum(x, y, string, num, den, ref_top);
}

// Natural-size variant: the HU font as-is (ratio 1:1, caps' ink top
// at y).  The shader browser's rows use this - at the M_ item patch
// height, a single row reads as a title.
static void M_DrawTextScaled1x(int x, int y, const char *string)
{
    int cap_top, cap_bottom;

    M_HUCapInk(&cap_top, &cap_bottom);

    M_DrawTextScaledNum(x, y, string, cap_bottom - cap_top,
                        cap_bottom - cap_top, 0);
}

// These keys evaluate to a "null" key in Vanilla Doom that allows weird
// jumping in the menus. Preserve this behavior for accuracy.

static boolean IsNullKey(int key)
{
    return key == KEY_PAUSE || key == KEY_CAPSLOCK
        || key == KEY_SCRLCK || key == KEY_NUMLOCK;
}

//
// CONTROL PANEL
//

//
// M_Responder
//
boolean M_Responder (event_t* ev)
{
    int             ch;
    int             key;
    int             i;
    static  int     mousewait = 0;
    static  int     mousey = 0;
    static  int     lasty = 0;
    static  int     mousex = 0;
    static  int     lastx = 0;
    int dir;

    // In testcontrols mode, none of the function keys should do anything
    // - the only key is escape to quit.

    if (testcontrols)
    {
        if (ev->type == ev_quit
         || (ev->type == ev_keydown
          && (ev->data1 == key_menu_activate || ev->data1 == key_menu_quit)))
        {
            I_Quit();
            return true;
        }

        return false;
    }

    // "close" button pressed on window?
    if (ev->type == ev_quit)
    {
        // First click on close button = bring up quit confirm message.
        // Second click on close button = confirm quit

        if (menuactive && messageToPrint && messageRoutine == M_QuitResponse)
        {
            M_QuitResponse(key_menu_confirm);
        }
        else
        {
            S_StartSound(NULL,sfx_swtchn);
            M_QuitDOOM(0);
        }

        return true;
    }

    // key is the key pressed, ch is the actual character typed
  
    ch = 0;
    key = -1;
	
    if (ev->type == ev_joystick)
    {
        // Simulate key presses from joystick events to interact with the menu.

        if (menuactive)
        {
            if (JOY_GET_DPAD(ev->data6) != JOY_DIR_NONE)
            {
                dir = JOY_GET_DPAD(ev->data6);
            }
            else if (JOY_GET_LSTICK(ev->data6) != JOY_DIR_NONE)
            {
                dir = JOY_GET_LSTICK(ev->data6);
            }
            else
            {
                dir = JOY_GET_RSTICK(ev->data6);
            }

            if (dir & JOY_DIR_UP)
            {
                key = key_menu_up;
                joywait = I_GetTime() + 5;
            }
            else if (dir & JOY_DIR_DOWN)
            {
                key = key_menu_down;
                joywait = I_GetTime() + 5;
            }
            if (dir & JOY_DIR_LEFT)
            {
                key = key_menu_left;
                joywait = I_GetTime() + 5;
            }
            else if (dir & JOY_DIR_RIGHT)
            {
                key = key_menu_right;
                joywait = I_GetTime() + 5;
            }

#define JOY_BUTTON_MAPPED(x) ((x) >= 0)
#define JOY_BUTTON_PRESSED(x) (JOY_BUTTON_MAPPED(x) && (ev->data1 & (1 << (x))) != 0)

            if (JOY_BUTTON_PRESSED(joybfire))
            {
                // Simulate a 'Y' keypress when Doom show a Y/N dialog with Fire button.
                if (messageToPrint && messageNeedsInput)
                {
                    key = key_menu_confirm;
                }
                // Simulate pressing "Enter" when we are supplying a save slot name
                else if (saveStringEnter)
                {
                    key = KEY_ENTER;
                }
                else
                {
                    // if selecting a save slot via joypad, set a flag
                    if (currentMenu == &SaveDef)
                    {
                        joypadSave = true;
                    }
                    key = key_menu_forward;
                }
                joywait = I_GetTime() + 5;
            }
            if (JOY_BUTTON_PRESSED(joybuse))
            {
                // Simulate a 'N' keypress when Doom show a Y/N dialog with Use button.
                if (messageToPrint && messageNeedsInput)
                {
                    key = key_menu_abort;
                }
                // If user was entering a save name, back out
                else if (saveStringEnter)
                {
                    key = KEY_ESCAPE;
                }
                else
                {
                    key = key_menu_back;
                }
                joywait = I_GetTime() + 5;
            }
        }
        if (JOY_BUTTON_PRESSED(joybmenu))
        {
            key = key_menu_activate;
            joywait = I_GetTime() + 5;
        }
    }
    else
    {
	if (ev->type == ev_mouse && mousewait < I_GetTime() && menuactive)
	{
	    mousey += ev->data3;
	    if (mousey < lasty-30)
	    {
		key = key_menu_down;
		mousewait = I_GetTime() + 5;
		mousey = lasty -= 30;
	    }
	    else if (mousey > lasty+30)
	    {
		key = key_menu_up;
		mousewait = I_GetTime() + 5;
		mousey = lasty += 30;
	    }
		
	    mousex += ev->data2;
	    if (mousex < lastx-30)
	    {
		key = key_menu_left;
		mousewait = I_GetTime() + 5;
		mousex = lastx -= 30;
	    }
	    else if (mousex > lastx+30)
	    {
		key = key_menu_right;
		mousewait = I_GetTime() + 5;
		mousex = lastx += 30;
	    }
		
	    if (ev->data1&1)
	    {
		key = key_menu_forward;
		mousewait = I_GetTime() + 15;
	    }
			
	    if (ev->data1&2)
	    {
		key = key_menu_back;
		mousewait = I_GetTime() + 15;
	    }
	}
	else
	{
	    if (ev->type == ev_keydown)
	    {
		key = ev->data1;
		ch = ev->data2;
	    }
	}
    }
    
    if (key == -1)
	return false;

    // Save Game string input
    if (saveStringEnter)
    {
	switch(key)
	{
	  case KEY_BACKSPACE:
	    if (saveCharIndex > 0)
	    {
		saveCharIndex--;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;

          case KEY_ESCAPE:
            saveStringEnter = 0;
            I_StopTextInput();
            M_StringCopy(savegamestrings[saveSlot], saveOldString,
                         SAVESTRINGSIZE);
            break;

	  case KEY_ENTER:
	    saveStringEnter = 0;
            I_StopTextInput();
	    if (savegamestrings[saveSlot][0])
		M_DoSave(saveSlot);
	    break;

	  default:
            // Savegame name entry. This is complicated.
            // Vanilla has a bug where the shift key is ignored when entering
            // a savegame name. If vanilla_keyboard_mapping is on, we want
            // to emulate this bug by using ev->data1. But if it's turned off,
            // it implies the user doesn't care about Vanilla emulation:
            // instead, use ev->data3 which gives the fully-translated and
            // modified key input.

            if (ev->type != ev_keydown)
            {
                break;
            }
            if (vanilla_keyboard_mapping)
            {
                ch = ev->data1;
            }
            else
            {
                ch = ev->data3;
            }

            ch = toupper(ch);

            if (ch != ' '
             && (ch - HU_FONTSTART < 0 || ch - HU_FONTSTART >= HU_FONTSIZE))
            {
                break;
            }

	    if (ch >= 32 && ch <= 127 &&
		saveCharIndex < SAVESTRINGSIZE-1 &&
		M_StringWidth(savegamestrings[saveSlot]) <
		(SAVESTRINGSIZE-2)*8)
	    {
		savegamestrings[saveSlot][saveCharIndex++] = ch;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;
	}
	return true;
    }

    // Take care of any messages that need input
    if (messageToPrint)
    {
	if (messageNeedsInput)
        {
            if (key != ' ' && key != KEY_ESCAPE
             && key != key_menu_confirm && key != key_menu_abort)
            {
                return false;
            }
	}

	menuactive = messageLastMenuActive;
	messageToPrint = 0;
	if (messageRoutine)
	    messageRoutine(key);

	menuactive = false;
	S_StartSound(NULL,sfx_swtchx);
	return true;
    }

    if ((devparm && key == key_menu_help) ||
        (key != 0 && key == key_menu_screenshot))
    {
	G_ScreenShot ();
	return true;
    }

    // F-Keys
    if (!menuactive)
    {
	if (key == key_menu_decscreen || key == KEYP_MINUS)   // Screen size down
        {
	    if (automapactive || chat_on)
		return false;
	    M_SizeDisplay(0);
	    S_StartSound(NULL,sfx_stnmov);
	    return true;
	}
        else if (key == key_menu_incscreen || key == KEYP_PLUS) // Screen size up
        {
	    if (automapactive || chat_on)
		return false;
	    M_SizeDisplay(1);
	    S_StartSound(NULL,sfx_stnmov);
	    return true;
	}
        else if (key == key_menu_help)     // Help key
        {
	    M_StartControlPanel ();

	    if (gameversion >= exe_ultimate)
	      currentMenu = &ReadDef2;
	    else
	      currentMenu = &ReadDef1;

	    itemOn = 0;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
	}
        else if (key == key_menu_save)     // Save
        {
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_SaveGame(0);
	    return true;
        }
        else if (key == key_menu_load)     // Load
        {
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_LoadGame(0);
	    return true;
        }
        else if (key == key_menu_volume)   // Sound Volume
        {
	    M_StartControlPanel ();
	    currentMenu = &SoundDef;
	    itemOn = sfx_vol;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
	}
        else if (key == key_menu_qsave)    // Quicksave
        {
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickSave();
	    return true;
        }
        else if (key == key_menu_endgame)  // End game
        {
	    S_StartSound(NULL,sfx_swtchn);
	    M_EndGame(0);
	    return true;
        }
        else if (key == key_menu_messages) // Toggle messages
        {
	    M_ChangeMessages(0);
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
        }
        else if (key == key_menu_qload)    // Quickload
        {
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickLoad();
	    return true;
        }
        else if (key == key_menu_quit)     // Quit DOOM
        {
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuitDOOM(0);
	    return true;
        }
        else if (key == key_menu_gamma)    // gamma toggle
        {
	    usegamma++;
	    if (usegamma > 4)
		usegamma = 0;
	    players[consoleplayer].message = DEH_String(gammamsg[usegamma]);
            I_SetPalette (W_CacheLumpName (DEH_String("PLAYPAL"),PU_CACHE));
	    return true;
	}
    }

    // Pop-up menu?
    if (!menuactive)
    {
	if (key == key_menu_activate)
	{
	    M_StartControlPanel ();
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
	}
	return false;
    }

    // Keys usable within menu

    // [CDVK] Shader browser: the rows are a sliding window over the
    // full preset list - scroll it here; the generic wrap below can
    // only reach the rows of the current window.
    if (currentMenu == &ShadersDef && M_ShadersScroll(key))
    {
        return true;
    }
    if (key == key_menu_down)
    {
        // Move down to next item

        do
	{
	    if (itemOn+1 > currentMenu->numitems-1)
		itemOn = 0;
	    else itemOn++;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);

	return true;
    }
    else if (key == key_menu_up)
    {
        // Move back up to previous item

	do
	{
	    if (!itemOn)
		itemOn = currentMenu->numitems-1;
	    else itemOn--;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);

	return true;
    }
    else if (key == key_menu_left)
    {
        // Slide slider left

	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(0);
	}
	return true;
    }
    else if (key == key_menu_right)
    {
        // Slide slider right

	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(1);
	}
	return true;
    }
    else if (key == key_menu_forward)
    {
        // Activate menu item

	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status)
	{
	    currentMenu->lastOn = itemOn;
	    if (currentMenu->menuitems[itemOn].status == 2)
	    {
		currentMenu->menuitems[itemOn].routine(1);      // right arrow
		S_StartSound(NULL,sfx_stnmov);
	    }
	    else
	    {
		currentMenu->menuitems[itemOn].routine(itemOn);
		S_StartSound(NULL,sfx_pistol);
	    }
	}
	return true;
    }
    else if (key == key_menu_activate)
    {
        // Deactivate menu

	currentMenu->lastOn = itemOn;
	M_ClearMenus ();
	S_StartSound(NULL,sfx_swtchx);
	return true;
    }
    else if (key == key_menu_back)
    {
        // Go back to previous menu

	currentMenu->lastOn = itemOn;
	if (currentMenu->prevMenu)
	{
	    currentMenu = currentMenu->prevMenu;
	    itemOn = currentMenu->lastOn;
	    S_StartSound(NULL,sfx_swtchn);
	}
	return true;
    }

    // Keyboard shortcut?
    // Vanilla Doom has a weird behavior where it jumps to the scroll bars
    // when the certain keys are pressed, so emulate this.

    else if (ch != 0 || IsNullKey(key))
    {
	for (i = itemOn+1;i < currentMenu->numitems;i++)
        {
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
        }

	for (i = 0;i <= itemOn;i++)
        {
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
        }
    }

    return false;
}



//
// M_StartControlPanel
//
void M_StartControlPanel (void)
{
    // intro might call this repeatedly
    if (menuactive)
	return;
    
    menuactive = 1;
    currentMenu = &MainDef;         // JDC
    itemOn = currentMenu->lastOn;   // JDC
}

// Display OPL debug messages - hack for GENMIDI development.

static void M_DrawOPLDev(void)
{
    char debug[1024];
    char *curr, *p;
    int line;

    I_OPL_DevMessages(debug, sizeof(debug));
    curr = debug;
    line = 0;

    for (;;)
    {
        p = strchr(curr, '\n');

        if (p != NULL)
        {
            *p = '\0';
        }

        M_WriteText(0, line * 8, curr);
        ++line;

        if (p == NULL)
        {
            break;
        }

        curr = p + 1;
    }
}

//
// M_Drawer
// Called after the view has been rendered,
// but before it has been blitted.
//
void M_Drawer (void)
{
    static short	x;
    static short	y;
    unsigned int	i;
    unsigned int	max;
    char		string[80];
    const char          *name;
    int			start;

    inhelpscreens = false;
    
    // Horiz. & Vertically center string and print it.
    if (messageToPrint)
    {
	start = 0;
	y = SCREENHEIGHT/2 - M_StringHeight(messageString) / 2;
	while (messageString[start] != '\0')
	{
	    boolean foundnewline = false;

            for (i = 0; messageString[start + i] != '\0'; i++)
            {
                if (messageString[start + i] == '\n')
                {
                    M_StringCopy(string, messageString + start,
                                 sizeof(string));
                    if (i < sizeof(string))
                    {
                        string[i] = '\0';
                    }

                    foundnewline = true;
                    start += i + 1;
                    break;
                }
            }

            if (!foundnewline)
            {
                M_StringCopy(string, messageString + start, sizeof(string));
                start += strlen(string);
            }

	    x = SCREENWIDTH/2 - M_StringWidth(string) / 2;
	    M_WriteText(x, y, string);
	    y += SHORT(hu_font[0]->height);
	}

	return;
    }

    if (opldev)
    {
        M_DrawOPLDev();
    }

    if (!menuactive)
	return;

    if (currentMenu->routine)
	currentMenu->routine();         // call Draw routine
    
    // DRAW MENU
    x = currentMenu->x;
    y = currentMenu->y;
    max = currentMenu->numitems;

    for (i=0;i<max;i++)
    {
        name = DEH_String(currentMenu->menuitems[i].name);

	if (name[0] && W_CheckNumForName(name) > 0)
	{
	    V_DrawPatchDirect (x, y, W_CacheLumpName(name, PU_CACHE));
	}
	y += LINEHEIGHT;
    }

    
    // DRAW SKULL
    V_DrawPatchDirect(x + SKULLXOFF, currentMenu->y - 5 + itemOn*LINEHEIGHT,
		      W_CacheLumpName(DEH_String(skullName[whichSkull]),
				      PU_CACHE));
}


//
// M_ClearMenus
//
void M_ClearMenus (void)
{
    menuactive = 0;
    // if (!netgame && usergame && paused)
    //       sendpause = true;
}




//
// M_SetupNextMenu
//
void M_SetupNextMenu(menu_t *menudef)
{
    currentMenu = menudef;
    itemOn = currentMenu->lastOn;
}


//
// M_Ticker
//
void M_Ticker (void)
{
    if (--skullAnimCounter <= 0)
    {
	whichSkull ^= 1;
	skullAnimCounter = 8;
    }
}


//
// M_Init
//
void M_Init (void)
{
    currentMenu = &MainDef;
    menuactive = 0;
    itemOn = currentMenu->lastOn;
    whichSkull = 0;
    skullAnimCounter = 10;
    screenSize = screenblocks - 3;
    messageToPrint = 0;
    messageString = NULL;
    messageLastMenuActive = menuactive;
    quickSaveSlot = -1;

    // Here we could catch other version dependencies,
    //  like HELP1/2, and four episodes.

    // The same hacks were used in the original Doom EXEs.

    if (gameversion >= exe_ultimate)
    {
        MainMenu[readthis].routine = M_ReadThis2;
        ReadDef2.prevMenu = NULL;
    }

    if (gameversion >= exe_final && gameversion <= exe_final2)
    {
        ReadDef2.routine = M_DrawReadThisCommercial;
    }

    if (gamemode == commercial)
    {
        MainMenu[readthis] = MainMenu[quitdoom];
        MainDef.numitems--;
        MainDef.y += 8;
        NewDef.prevMenu = &MainDef;
        ReadDef1.routine = M_DrawReadThisCommercial;
        ReadDef1.x = 330;
        ReadDef1.y = 165;
        ReadMenu1[rdthsempty1].routine = M_FinishReadThis;
    }

    // Versions of doom.exe before the Ultimate Doom release only had
    // three episodes; if we're emulating one of those then don't try
    // to show episode four. If we are, then do show episode four
    // (should crash if missing).
    if (gameversion < exe_ultimate)
    {
        EpiDef.numitems--;
    }
    // chex.exe shows only one episode.
    else if (gameversion == exe_chex)
    {
        EpiDef.numitems = 1;
    }

    opldev = M_CheckParm("-opldev") > 0;
}

