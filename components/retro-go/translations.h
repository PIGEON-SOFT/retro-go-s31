#include "rg_localization.h"

static const char *language_names[RG_LANG_MAX] = {
    [RG_LANG_EN] = "English",
    [RG_LANG_ZH] = "中文",
};

static const char *translations[][RG_LANG_MAX] =
{
    {
        [RG_LANG_EN] = "Never",
    },
    {
        [RG_LANG_EN] = "Always",
    },
    {
        [RG_LANG_EN] = "Composite",
    },
    {
        [RG_LANG_EN] = "NES Classic",
    },
    {
        [RG_LANG_EN] = "NTSC",
    },
    {
        [RG_LANG_EN] = "PVM",
    },
    {
        [RG_LANG_EN] = "Smooth",
    },
    {
        [RG_LANG_EN] = "To start, try: 1 or * or #",
    },
    {
	    [RG_LANG_EN] = "Full",
    },
    {
        [RG_LANG_EN] = "Yes",
    },
    {
        [RG_LANG_EN] = "Select file",
    },
    {
        [RG_LANG_EN] = "Language",
    },
    {
        [RG_LANG_EN] = "Language changed!",
    },
    {
        [RG_LANG_EN] = "For these changes to take effect you must restart your device.\nrestart now?",
    },
    {
        [RG_LANG_EN] = "Wi-Fi profile",
    },
    {
        [RG_LANG_EN] = "Language",
    },
    {
        [RG_LANG_EN] = "Options",
    },
    {
        [RG_LANG_EN] = "About Retro-Go",
    },
    {
        [RG_LANG_EN] = "Reset all settings?",
    },
    {
        [RG_LANG_EN] = "Initializing...",
    },
    {
        [RG_LANG_EN] = "Host Game (P1)",
    },
    {
        [RG_LANG_EN] = "Find Game (P2)",
    },
    {
        [RG_LANG_EN] = "Netplay",
    },
    {
        [RG_LANG_EN] = "ROMs not identical. Continue?",
    },
    {
        [RG_LANG_EN] = "Exchanging info...",
    },
    {
        [RG_LANG_EN] = "Unable to find host!",
    },
    {
        [RG_LANG_EN] = "Connection failed!",
    },
    {
        [RG_LANG_EN] = "Waiting for peer...",
    },
    {
        [RG_LANG_EN] = "Unknown status...",
    },
    {
        [RG_LANG_EN] = "On",
    },
    {
        [RG_LANG_EN] = "Keyboard",
    },
    {
        [RG_LANG_EN] = "Joystick",
    },
    {
        [RG_LANG_EN] = "Input",
    },
    {
        [RG_LANG_EN] = "Crop",
    },
    {
        [RG_LANG_EN] = "BIOS file missing!",
    },
    {
        [RG_LANG_EN] = "YM2612 audio ",
    },
    {
        [RG_LANG_EN] = "SN76489 audio",
    },
    {
        [RG_LANG_EN] = "Z80 emulation",
    },
    {
        [RG_LANG_EN] = "Launcher options",
    },
    {
        [RG_LANG_EN] = "Emulator options",
    },
    {
        [RG_LANG_EN] = "Date",
    },
    {
        [RG_LANG_EN] = "Files:",
    },
    {
        [RG_LANG_EN] = "Download complete!",
    },
    {
        [RG_LANG_EN] = "Reboot to flash?",
    },
    {
        [RG_LANG_EN] = "Available Releases",
    },
    {
        [RG_LANG_EN] = "Received empty list!",
    },
    {
        [RG_LANG_EN] = "Gamma Boost",
    },
    {
        [RG_LANG_EN] = "Day",
    },
    {
        [RG_LANG_EN] = "Hour",
    },
    {
        [RG_LANG_EN] = "Min",
    },
    {
        [RG_LANG_EN] = "Sec",
    },
    {
        [RG_LANG_EN] = "Sync",
    },
    {
        [RG_LANG_EN] = "RTC config",
    },
    {
        [RG_LANG_EN] = "Palette",
    },
    {
        [RG_LANG_EN] = "RTC config",
    },
    {
        [RG_LANG_EN] = "SRAM autosave",
    },
    {
        [RG_LANG_EN] = "Enable BIOS",
    },
    {
        [RG_LANG_EN] = "Name",
    },
    {
        [RG_LANG_EN] = "Artist",
    },
    {
        [RG_LANG_EN] = "Copyright",
    },
    {
        [RG_LANG_EN] = "Playing",
    },
    {
        [RG_LANG_EN] = "Palette",
    },
    {
        [RG_LANG_EN] = "Overscan",
    },
    {
        [RG_LANG_EN] = "Crop sides",
    },
    {
        [RG_LANG_EN] = "Sprite limit",
    },
    {
        [RG_LANG_EN] = "Overscan",
    },
    {
        [RG_LANG_EN] = "Palette",
    },
    {
        [RG_LANG_EN] = "Profile",
    },
    {
        [RG_LANG_EN] = "<profile name>",
    },
    {
        [RG_LANG_EN] = "Controls",
    },
    {
        [RG_LANG_EN] = "Audio enable",
    },
    {
        [RG_LANG_EN] = "Audio filter",
    },


    // rg_gui.c
    {
        [RG_LANG_EN] = "Folder is empty.",
    },
    {
        [RG_LANG_EN] = "No",
    },
    {
        [RG_LANG_EN] = "OK",
    },
    {
        [RG_LANG_EN] = "On",
    },
    {
        [RG_LANG_EN] = "Off",
    },
    {
        [RG_LANG_EN] = "Horiz",
    },
    {
        [RG_LANG_EN] = "Vert",
    },
    {
        [RG_LANG_EN] = "Both",
    },
    {
        [RG_LANG_EN] = "Fit",
    },
    {
        [RG_LANG_EN] = "Full ",
    },
    {
        [RG_LANG_EN] = "Zoom",
    },

    // Led options
    {
        [RG_LANG_EN] = "LED options",
    },
    {
        [RG_LANG_EN] = "System activity",
    },
    {
        [RG_LANG_EN] = "Disk activity",
    },
    {
        [RG_LANG_EN] = "Low battery",
    },
    {
        [RG_LANG_EN] = "Default",
    },
    {
        [RG_LANG_EN] = "<None>",
    },

    // Wifi
    {
        [RG_LANG_EN] = "Not connected",
    },
    {
        [RG_LANG_EN] = "Connecting...",
    },
    {
        [RG_LANG_EN] = "Disconnecting...",
    },
    {
        [RG_LANG_EN] = "(empty)",
    },
    {
        [RG_LANG_EN] = "Wi-Fi AP",
    },
    {
        [RG_LANG_EN] = "Start access point?\n\nSSID: retro-go\nPassword: retro-go\n\nBrowse: http://192.168.4.1/",
    },
    {
        [RG_LANG_EN] = "Wi-Fi enable",
    },
    {
        [RG_LANG_EN] = "Wi-Fi access point",
    },
    {
        [RG_LANG_EN] = "Network",
    },
    {
        [RG_LANG_EN] = "IP address",
    },

    // retro-go settings
    {
        [RG_LANG_EN] = "Brightness",
    },
    {
        [RG_LANG_EN] = "Volume",
    },
    {
        [RG_LANG_EN] = "Audio out",
    },
    {
        [RG_LANG_EN] = "Font type",
    },
    {
        [RG_LANG_EN] = "Theme",
    },
    {
        [RG_LANG_EN] = "Show clock",
        [RG_LANG_ZH] = "显示时钟",
    },
    {
        [RG_LANG_EN] = "Show FPS",
        [RG_LANG_ZH] = "显示帧率",
    },
    {
        [RG_LANG_EN] = "Timezone",
    },
    {
        [RG_LANG_EN] = "Wi-Fi options",
    },

    // app settings
    {
        [RG_LANG_EN] = "Scaling",
    },
    {
        [RG_LANG_EN] = "Factor",
    },
    {
        [RG_LANG_EN] = "Filter",
    },
    {
        [RG_LANG_EN] = "Border",
    },
    {
        [RG_LANG_EN] = "Speed",
    },

    // about menu
    {
        [RG_LANG_EN] = "Version",
    },
    {
        [RG_LANG_EN] = "Date",
    },
    {
        [RG_LANG_EN] = "Target",
    },
    {
        [RG_LANG_EN] = "Website",
    },
    {
        [RG_LANG_EN] = "Options",
    },
    {
        [RG_LANG_EN] = "View credits",
    },
    {
        [RG_LANG_EN] = "Debug menu",
    },
    {
        [RG_LANG_EN] = "Reset settings",
    },

    // save slot
    {
        [RG_LANG_EN] = "Slot 0",
    },
    {
        [RG_LANG_EN] = "Slot 1",
    },
    {
        [RG_LANG_EN] = "Slot 2",

    },
    {
        [RG_LANG_EN] = "Slot 3",
    },

    // game menu
    {
        [RG_LANG_EN] = "Save & Continue",
    },
    {
        [RG_LANG_EN] = "Save & Quit",
    },
    {
        [RG_LANG_EN] = "Load game",
    },
    {
        [RG_LANG_EN] = "Reset",
    },
    {
        [RG_LANG_EN] = "Netplay",
    },
    {
        [RG_LANG_EN] = "About",
    },
    {
        [RG_LANG_EN] = "Quit",
    },
    {
        [RG_LANG_EN] = "Soft reset",
    },
    {
        [RG_LANG_EN] = "Hard reset",
    },

    {
        [RG_LANG_EN] = "Reset Emulation?",
    },
    {
        [RG_LANG_EN] = "Save",
    },
    {
        [RG_LANG_EN] = "Load",
    },
    // end of rg_gui.c


    // main.c
    {
        [RG_LANG_EN] = "Show",
    },
    {
        [RG_LANG_EN] = "Hide",
    },
    {
        [RG_LANG_EN] = "Tabs Visibility",
    },

    // scroll modes
    {
        [RG_LANG_EN] = "Center",
    },
    {
        [RG_LANG_EN] = "Paging",
    },

    // start screen
    {
        [RG_LANG_EN] = "Auto",
    },
    {
        [RG_LANG_EN] = "Carousel",
    },
    {
        [RG_LANG_EN] = "Browser",
    },

    // preview
    {
        [RG_LANG_EN] = "None",
    },
    {
        [RG_LANG_EN] = "Cover,Save",
    },
    {
        [RG_LANG_EN] = "Save,Cover",
    },
    {
        [RG_LANG_EN] = "Cover only",
    },
    {
        [RG_LANG_EN] = "Save only",
    },

    // startup app
    {
        [RG_LANG_EN] = "Last game",
    },
    {
        [RG_LANG_EN] = "Launcher",
    },

    // launcher options
    {
        [RG_LANG_EN] = "Launcher Options",
    },
    {
        [RG_LANG_EN] = "Color theme",
    },
    {
        [RG_LANG_EN] = "Preview",
    },
    {
        [RG_LANG_EN] = "Scroll mode",
    },
    {
        [RG_LANG_EN] = "Start screen",
    },
    {
        [RG_LANG_EN] = "Hide tabs",
    },
    {
        [RG_LANG_EN] = "File server",
    },
    {
        [RG_LANG_EN] = "Startup app",
    },
    {
        [RG_LANG_EN] = "Build CRC cache",
    },
    {
        [RG_LANG_EN] = "Check for updates",
    },
    {
        [RG_LANG_EN] = "HTTP Server Busy...",
    },
    {
        [RG_LANG_EN] = "SD Card Error",
    },
    {
        [RG_LANG_EN] = "Storage mount failed.\nMake sure the card is FAT32.",
    },
    // end of main.c


    // applications.c
    {
        [RG_LANG_EN] = "Scanning %s %d/%d",
    },
    // message when no rom
    {
        [RG_LANG_EN] = "Welcome to Retro-Go!",
    },
    {
        [RG_LANG_EN] = "Place roms in folder: %s",
    },
    {
        [RG_LANG_EN] = "With file extension: %s",
    },
    {
        [RG_LANG_EN] = "You can hide this tab in the menu",
    },
    {
        [RG_LANG_EN] = "You have no %s games",
    },
    {
        [RG_LANG_EN] = "File not found",
    },

    // rom options
    {
        [RG_LANG_EN] = "Name",
    },
    {
        [RG_LANG_EN] = "Folder",
    },
    {
        [RG_LANG_EN] = "Size",
    },
    {
        [RG_LANG_EN] = "CRC32",
    },
    {
        [RG_LANG_EN] = "Delete file",
    },
    {
        [RG_LANG_EN] = "Close",
    },
    {
        [RG_LANG_EN] = "File properties",
    },
    {
        [RG_LANG_EN] = "Delete selected file?",
    },


    // in-game menu
    {
        [RG_LANG_EN] = "Resume game",
    },
    {
        [RG_LANG_EN] = "New game",
    },
    {
        [RG_LANG_EN] = "Del favorite",
    },
    {
        [RG_LANG_EN] = "Add favorite",
    },
    {
        [RG_LANG_EN] = "Delete save",
    },
    {
        [RG_LANG_EN] = "Properties",
    },
    {
        [RG_LANG_EN] = "Resume",
    },
    {
        [RG_LANG_EN] = "Delete save?",
    },
    {
        [RG_LANG_EN] = "Delete sram file?",
    },
    // end of applications.c


    // rg_system.c
    {
        [RG_LANG_EN] = "App unresponsive... Hold MENU to quit!",
    },
    {
        [RG_LANG_EN] = "Reset all settings",
    },
    {
        [RG_LANG_EN] = "Reboot to factory ",
    },
    {
        [RG_LANG_EN] = "Reboot to launcher",
    },
    {
        [RG_LANG_EN] = "Recovery mode",
    },
    {
        [RG_LANG_EN] = "System Panic!",
    },
    {
        [RG_LANG_EN] = "Save failed",
    },
    // end of rg_system.c
};
