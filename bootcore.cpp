// bootcore.cpp
// 2019, Aitor Gomez Garcia (spark2k06@gmail.com)
// Thanks to Sorgelig and BBond007 for their help and advice in the development of this feature.

#include "file_io.h"
#include "cfg.h"
#include "fpga_io.h"
#include "hardware.h"
#include "support/arcade/mra_loader.h"
#include "bootcore.h"
#include "user_io.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

enum bootcoreType
{
	BOOTCORE_NONE,
	BOOTCORE_LASTCORE,
	BOOTCORE_LASTCORE_EXACT,
	BOOTCORE_CORENAME,
	BOOTCORE_CORENAME_EXACT
};

static char rbf_name[256];
static char core_path[256];
bootcoreType launch_type = BOOTCORE_NONE;
static unsigned long launch_time;


void makeRBFName(char *str)
{
	char *p = strrchr(str, '/');
	if (!p) return;

	char *spl = strrchr(p + 1, '.');
	if (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")))
	{
		*spl = 0;
	}

	memmove(str, p + 1, strlen(p + 1) + 1);
}


bool isExactcoreName(char *path)
{
	char *spl = strrchr(path, '.');
	return (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")));
}

void makeCoreName(char *path)
{
	char *orig = path;
	char *spl = strrchr(path, '.');
	if (spl && !strcmp(spl, ".rbf"))
	{
		*spl = '\0';
	}
	else
	{
		*path = '\0';
		return;
	}

	if ((spl = strrchr(path, '/')) != NULL)
	{
		path = spl + 1;
	}

	if ((spl = strrchr(path, '_')) != NULL)
	{
		*spl = 0;
	}

	if( orig != path )
	{
		memmove(orig, path, strlen(path) + 1);
	}
}

void makeExactCoreName(char *path)
{
	char *spl;
	if ((spl = strrchr(path, '/')) != NULL)
	{
		memmove(path, spl + 1, strlen(spl + 1) + 1);
	}
}


char *findCore(const char *name, char *coreName, int indent)
{
	char *spl;
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir(name)))
	{
		return NULL;
	}

	char *indir;
	char* path = new char[256];
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			if (entry->d_name[0] != '_')
				continue;
			snprintf(path, 256, "%s/%s", name, entry->d_name);
			indir = findCore(path, coreName, indent + 2);
			if (indir != NULL)
			{
				closedir(dir);
				delete[] path;
				return indir;
			}
		}
		else {
			snprintf(path, 256, "%s/%s", name, entry->d_name);
			if (strstr(path, coreName) != NULL) {
				spl = strrchr(path, '.');
				if (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")))
				{
					closedir(dir);
					return path;
				}
			}
		}
	}
	closedir(dir);
	delete[] path;
	return NULL;
}

void bootcore_init(const char *path)
{
	char bootcore[256];
	int len = FileLoadConfig("lastcore.dat", bootcore, sizeof(bootcore));
	bootcore[len] = 0;

	// determine type
	if( !strcmp( cfg.bootcore, "lastcore" ) )
	{
		launch_type = BOOTCORE_LASTCORE;
	}
	else if( !strcmp( cfg.bootcore, "lastexactcore" ) )
	{
		launch_type = BOOTCORE_LASTCORE_EXACT;
	}
	else if( isExactcoreName(cfg.bootcore) )
	{
		launch_type = BOOTCORE_CORENAME_EXACT;
		strcpy(bootcore, cfg.bootcore);
	}
	else
	{
		launch_type = BOOTCORE_CORENAME;
		strcpy(bootcore, cfg.bootcore);
	}

	// if we are booting a core
	if( path[0] != '\0' )
	{
		if( !is_menu() && ( launch_type == BOOTCORE_LASTCORE || launch_type == BOOTCORE_LASTCORE_EXACT ) )
		{
			if( strcmp(path, bootcore) )
			{
				FileSaveConfig("lastcore.dat", (char*)path, strlen(path));
			}
		}
		launch_type = BOOTCORE_NONE;
		return;
	}

	// clean up name
	if( launch_type == BOOTCORE_LASTCORE_EXACT || launch_type == BOOTCORE_CORENAME_EXACT || isMraName(bootcore) )
	{
		makeExactCoreName(bootcore);
	}
	else if( launch_type == BOOTCORE_LASTCORE )
	{	
		makeCoreName(bootcore);
	}

	// no valid bootcore
	if( bootcore[0] == '\0' )
	{
		launch_type = BOOTCORE_NONE;
		return;
	}

	// find the core
	char *found_path = findCore(getRootDir(), bootcore, 0);
	if (found_path == NULL)
	{
		launch_type = BOOTCORE_NONE;
		return;
	}

	char rootDir[256];
	sprintf(rootDir, "%s/", getRootDir());
	if( strncasecmp( found_path, rootDir, strlen(rootDir) ))
	{
		strcpy(core_path, found_path);
	}
	else
	{
		strcpy(core_path, found_path + strlen(rootDir));
	}
	delete[] found_path;

	strcpy(rbf_name, core_path);
	makeRBFName(rbf_name);

	if( cfg.bootcore_timeout )
	{
		launch_time = GetTimer(cfg.bootcore_timeout * 1000UL);
	}
	else
	{
		launch_time = 0;
		bootcore_launch();
		launch_type = BOOTCORE_NONE;
	}
}

void bootcore_launch()
{
	if( isMraName(core_path) )
	{
		arcade_load(getFullPath(core_path));
	}
	else
	{
		fpga_load_rbf(core_path);
	}
}

bool bootcore_pending()
{
	return launch_type != BOOTCORE_NONE;
}

bool bootcore_ready()
{
	return CheckTimer(launch_time);
}

unsigned int bootcore_delay()
{
	return cfg.bootcore_timeout * 1000;
}

unsigned int bootcore_remaining()
{
	unsigned long curtime = GetTimer(0);
	if( curtime >= launch_time )
	{
		return 0;
	}
	else
	{
		return ( launch_time - curtime );
	}
}

const char *bootcore_type()
{
	switch( launch_type )
	{
		case BOOTCORE_LASTCORE: return "lastcore";
		case BOOTCORE_LASTCORE_EXACT: return "lastexactcore";
		case BOOTCORE_CORENAME_EXACT: return "exactcorename";
		case BOOTCORE_CORENAME: return "corename";
		default: break;
	}
	return "none";
}

const char *bootcore_name()
{
	return rbf_name;
}

