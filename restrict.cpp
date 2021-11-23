#include "restrict.h"
#include "input.h"

#include <string.h>

struct Restrictions
{
    bool settings;
    bool browsing;
    bool options;
    bool cores;
    bool unlock;
    bool cheats;
    bool dip_switches;
};

static Restrictions restrictions;
static bool restrictions_enabled = true;
static bool any_restrictions = false;
static char unlockCode[24];
static char enteredUnlockCode[24];

void Restrict_Init(const char *config, const char *code)
{
    memset(&restrictions, 0, sizeof(restrictions));
    any_restrictions = false;

    strncpy( unlockCode, code, sizeof(unlockCode) - 1);

    if( unlockCode[0] == 0 )
    {
        restrictions.unlock = true;
    }

    const char *end = config + strlen(config);

    const char *p = config;

    while( p < end )
    {
        const char *e = strchr(p, ',');
        if(e == nullptr) e = end;

        int len = e - p;
        if( !strncasecmp(p, "settings", len) )
        {
            restrictions.settings = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "browsing", len) )
        {
            restrictions.browsing = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "options", len) )
        {
            restrictions.options = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "cores", len) )
        {
            restrictions.cores = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "unlock", len) )
        {
            restrictions.unlock = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "cheats", len) )
        {
            restrictions.cheats = true;
            any_restrictions = true;
        }
        else if( !strncasecmp(p, "dip_switches", len) )
        {
            restrictions.dip_switches = true;
            any_restrictions = true;
        }

        p = e + 1;
    }
}

void Restrict_Enable()
{
    restrictions_enabled = true;
}

void Restrict_Disable()
{
    restrictions_enabled = false;
}

bool Restrict_AnySpecified()
{
    return any_restrictions;
}

bool Restrict_Enabled()
{
    return restrictions_enabled;
}

bool Restrict_Settings()
{
    return restrictions_enabled && restrictions.settings;
}

bool Restrict_Unlock()
{
    return restrictions_enabled && restrictions.unlock;
}

bool Restrict_FileBrowsing()
{
    return restrictions_enabled && restrictions.browsing;
}

bool Restrict_Cores()
{
    return restrictions_enabled && restrictions.cores;
}

bool Restrict_Cheats()
{
    return restrictions_enabled && restrictions.cheats;
}

bool Restrict_DIPSwitches()
{
    return restrictions_enabled && restrictions.dip_switches;
}

bool Restrict_Options( RestrictOverride override )
{
    if( !restrictions_enabled )
    {
        return false;
    }

    switch( override )
    {
        case RestrictOverride::None: return restrictions.options;
        case RestrictOverride::Allowed: return false;
        case RestrictOverride::Restricted: return restrictions.options;
    }
    return false;
}

bool Restrict_Toggle( RestrictOverride override )
{
    if( !restrictions_enabled )
    {
        return false;
    }

    switch( override )
    {
        case RestrictOverride::None: return false;
        case RestrictOverride::Allowed: return false;
        case RestrictOverride::Restricted: return restrictions.options;
    }
    return false;
}

void Restrict_StartUnlock()
{
    enteredUnlockCode[0] = '\0';
}

int Restrict_UnlockLength()
{
    return strlen(unlockCode);
}


int Restrict_HandleUnlock(uint32_t keycode)
{
    int len = strlen(enteredUnlockCode);
    if( keycode == 0 )
    {
        return len;
    }

    if( keycode & UPSTROKE )
    {
        if( !strcmp( unlockCode, enteredUnlockCode ) )
        {
            Restrict_Disable();
            return -1;
        }

        if( len >= (int)strlen( unlockCode ) )
        {
            return -1;
        }

        return len;
    }

    char c = 0;
    switch( keycode )
    {
        case KEY_UP: c = 'U'; break;
        case KEY_DOWN: c = 'D'; break;
        case KEY_LEFT: c = 'L'; break;
        case KEY_RIGHT: c = 'R'; break;
        case KEY_ENTER:
        case KEY_SPACE:
		case KEY_KPENTER:
            c = 'A';
            break;
        case KEY_BACK:
        case KEY_BACKSPACE:
        case KEY_ESC:
            c = 'B';
            break;
        default: break;
    }

    if( c )
    {
        enteredUnlockCode[len] = c;
        enteredUnlockCode[len + 1] = '\0';
        len++;
    }

    return len;
}
