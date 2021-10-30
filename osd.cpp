/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

This is the Minimig OSD (on-screen-display) handler.

2012-02-09 - Split character rom out to separate header file, with upper 128 entries
as rotated copies of the first 128 entries.  -- AMR

29-12-2006 - created
30-12-2006 - improved and simplified
-- JB --
2008-10-04 - ARM version
2008-10-26 - added cpu and floppy configuration functions
2008-12-31 - added enable HDD command
2009-02-03 - full keyboard support
2009-06-23 - hires OSD display
2009-08-23 - adapted ConfigIDE() - support for 2 hardfiles
*/

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osd.h"
#include "spi.h"

#include "charrom.h"
#include "logo.h"
#include "user_io.h"
#include "hardware.h"

#include "support.h"

#define OSDLINELEN       256       // single line length in bytes
#define OSD_CMD_WRITE    0x20      // OSD write video data command
#define OSD_CMD_ENABLE   0x41      // OSD enable command
#define OSD_CMD_DISABLE  0x40      // OSD disable command
#define OSD_CMD_PALETTE  0x60      // OSD palette
#define OSD_CMD_TILEDATA 0x80      // OSD tile pixel data
#define OSD_CMD_TILEMAP  0xc0      // OSD tile map


static int osd_size = 8;

void OsdSetSize(int n)
{
	osd_size = n;
}

int OsdGetSize()
{
	return osd_size;
}

//
// TILES

#define NUM_TILES 128
#define TILE_MAP_WIDTH 32
#define TILE_MAP_HEIGHT 32
#define TILE_MAP_NUM_LAYERS 2
#define TILE_PLANES 4
static uint32_t tile_inuse_prev[NUM_TILE_BITS];
static uint32_t tile_inuse[NUM_TILE_BITS];
static uint8_t tile_to_char[NUM_TILES];
static uint8_t char_to_tile[256];
static uint8_t tile_data[NUM_TILES * 8 * TILE_PLANES];
static uint16_t tile_map[TILE_MAP_WIDTH * TILE_MAP_HEIGHT * TILE_MAP_NUM_LAYERS];
static uint16_t tile_palette[32];
static uint16_t default_tile_palette[32] = {
	0x000, 0xeff, 0xeff, 0x000, 0x000, 0x344, 0xeff, 0x344,
	0x000, 0x546, 0x676, 0xd75, 0x59d, 0x6b6, 0xec7, 0xeff,
	0xeff, 0xec7, 0x6b6, 0x59d, 0xd75, 0x676, 0x546, 0x000,
	0x000, 0x000, 0xfff, 0x811, 0x130, 0xda3, 0xbd5, 0x788,
//	0x788, 0x000, 0xfff, 0x811, 0x130, 0xda3, 0xbd5, 0x000
};
static bool palette_dirty;

void TileInit()
{
	static bool inited = false;

	if( !inited )
	{
		inited = true;
		memset( tile_inuse, 0, sizeof( tile_inuse ) );
		memset( char_to_tile, 0xff, sizeof( char_to_tile ) );
		memset( tile_to_char, 0, sizeof( tile_to_char ) );
		tile_inuse[0] = 1;
		memcpy(tile_palette, default_tile_palette, sizeof(default_tile_palette));
		palette_dirty = true;
	}
}

static uint8_t FindFreeTile()
{
	for( int idx = 0; idx < NUM_TILE_BITS; idx++ )
	{
		if( tile_inuse[idx] == 0xffffffff )
			continue;

		uint32_t bit = 31 - __builtin_clz(~tile_inuse[idx]);
		uint8_t tile_idx = ( idx * 32 ) + bit;
		tile_inuse[idx] |= 1 << bit;
		if( tile_to_char[tile_idx] )
		{
			char_to_tile[tile_to_char[tile_idx]] = 0xff;
			tile_to_char[tile_idx] = 0;
		}

		return tile_idx;
	}

	return 0;
}

static uint8_t CharToTile( int c )
{
	if( char_to_tile[c] != 0xff )
	{
		uint8_t tile_idx = char_to_tile[c];
		tile_inuse[tile_idx / 32] |= 1 << (tile_idx % 32);
		return char_to_tile[c];
	}

	uint8_t tile_idx = FindFreeTile();
	tile_to_char[tile_idx] = (uint8_t)c;
	char_to_tile[c] = tile_idx;

	uint8_t *p = &charfont[c][0];
	uint8_t *t = &tile_data[tile_idx * 8];
	for( int x = 0; x < 8; x++ )
	{
		uint8_t row = 0;
		for( int y = 0; y < 8; y++ )
		{
			if( p[y] & ( 1 << x ) )
				row |= 1 << y;
		}
		t[x] = row;
	}

	return tile_idx;
}

static void SetTile( int x, int y, int layer, int palette, uint8_t tile_idx )
{
	if( x < 0 || x >= TILE_MAP_WIDTH )
		return;

	if( y < 0 || y >= TILE_MAP_HEIGHT )
		return;

	if( layer < 0 || layer >= TILE_MAP_NUM_LAYERS )
		return;
	
	int pos = x + ( y * TILE_MAP_WIDTH ) + ( layer * TILE_MAP_WIDTH * TILE_MAP_HEIGHT );
	tile_map[pos] = ( 1 << 15 ) | ( ( palette & 7 ) << 10 ) | ( 3 << 7 ) | ( tile_idx & 0x7f );
}

static void ClearTileRow( int y, int layer, int palette )
{
	if( y < 0 || y >= TILE_MAP_HEIGHT )
		return;

	if( layer < 0 || layer >= TILE_MAP_NUM_LAYERS )
		return;
	
	int pos = ( y * TILE_MAP_WIDTH ) + ( layer * TILE_MAP_WIDTH * TILE_MAP_HEIGHT );
	for( int x = 0; x < TILE_MAP_WIDTH; x++ )
		tile_map[pos + x] = ( 1 << 15 ) | ( ( palette & 7 ) << 10 );
}

static void ClearTileLayer( int layer, int palette )
{
	if( layer < 0 || layer >= TILE_MAP_NUM_LAYERS )
		return;
	
	for( int y = 0; y < TILE_MAP_HEIGHT; y++ )
		ClearTileRow( y, layer, palette );
}

//
//

struct star
{
	int x, y;
	int dx, dy;
};


struct star stars[64];
static uint8_t osdbuf[256 * 32];
static int  osdbufpos = 0;
static int  osdset = 0;

char framebuffer[16][256];
static void framebuffer_clear()
{
	memset(framebuffer, 0, sizeof(framebuffer));
}

static void framebuffer_plot(int x, int y)
{
	framebuffer[y / 8][x] |= (1 << (y & 7));
}

void StarsInit()
{
	srand(time(NULL));
	for (int i = 0; i<64; ++i)
	{
		stars[i].x = (rand() % 228) << 4;	// X centre
		stars[i].y = (rand() % 128) << 4;	// Y centre
		stars[i].dx = -(rand() & 7) - 3;
		stars[i].dy = 0;
	}
}

void StarsUpdate()
{
	framebuffer_clear();
	for (int i = 0; i<64; ++i)
	{
		stars[i].x += stars[i].dx;
		stars[i].y += stars[i].dy;
		if ((stars[i].x<0) || (stars[i].x>(228 << 4)) ||
			(stars[i].y<0) || (stars[i].y>(128 << 4)))
		{
			stars[i].x = 228 << 4;
			stars[i].y = (rand() % 128) << 4;
			stars[i].dx = -(rand() & 7) - 3;
			stars[i].dy = 0;
		}
		framebuffer_plot(stars[i].x >> 4, stars[i].y >> 4);
	}
	osdset = -1;
}


// time delay after which file/dir name starts to scroll
#define SCROLL_DELAY 1000
#define SCROLL_DELAY2 10
#define SCROLL_DELAY3 50

static unsigned long scroll_offset[2] = {}; // file/dir name scrolling position
static unsigned long scroll_timer[2] = {};  // file/dir name scrolling timer

static int arrow;
static unsigned char titlebuffer[256];

static void rotatechar(unsigned char *in, unsigned char *out)
{
	int a;
	int b;
	int c;
	for (b = 0; b<8; ++b)
	{
		a = 0;
		for (c = 0; c<8; ++c)
		{
			a <<= 1;
			a |= (in[c] >> b) & 1;
		}
		out[b] = a;
	}
}

#define OSDHEIGHT (uint)(osd_size*8)

void OsdSetTitle(const char *s, int a)
{
	// Compose the title, condensing character gaps
	arrow = a;
	int zeros = 0;
	uint i = 0, j = 0;
	uint outp = 0;
	while (1)
	{
		int c = s[i++];
		if (c && (outp<OSDHEIGHT-8))
		{
			unsigned char *p = &charfont[c][0];
			for (j = 0; j<8; ++j)
			{
				unsigned char nc = *p++;
				if (nc)
				{
					zeros = 0;
					titlebuffer[outp++] = nc;
				}
				else if (zeros == 0 || (c == ' ' && zeros < 5))
				{
					titlebuffer[outp++] = 0;
					zeros++;
				}
				if (outp>sizeof(titlebuffer)) break;
			}
		}
		else break;
	}
	for (i = outp; i<OSDHEIGHT; i++)
	{
		titlebuffer[i] = 0;
	}

	// Now centre it:
	uint c = (OSDHEIGHT - 1 - outp) / 2;
	memmove(titlebuffer + c, titlebuffer, outp);

	for (i = 0; i<c; ++i) titlebuffer[i] = 0;

	// Finally rotate it.
	for (i = 0; i<OSDHEIGHT; i += 8)
	{
		unsigned char tmp[8];
		rotatechar(&titlebuffer[i], tmp);
		for (c = 0; c<8; ++c)
		{
			titlebuffer[i + c] = tmp[c];
		}
	}
}

void OsdSetArrow(int a)
{
	arrow = a;
}

void OsdWrite(unsigned char n, const char *s, unsigned char invert, unsigned char stipple, char usebg, int maxinv, int mininv)
{
	OsdWriteOffset(n, s, invert, stipple, 0, 0, usebg, maxinv, mininv);
}

static void osd_start(int line)
{
	line = line & 0x1F;
	osdset |= 1 << line;
	osdbufpos = line * 256;
	ClearTileRow(line, 0, 0);
}

static void draw_title(const unsigned char *p)
{
	// left white border
	osdbuf[osdbufpos++] = 0xff;
	osdbuf[osdbufpos++] = 0xff;
	osdbuf[osdbufpos++] = 0xff;

	for (int i = 0; i < 8; i++)
	{
		osdbuf[osdbufpos++] = 255 ^ *p;
		osdbuf[osdbufpos++] = 255 ^ *p++;
	}

	// right white border
	osdbuf[osdbufpos++] = 0xff;

	// blue gap
	osdbuf[osdbufpos++] = 0;
	osdbuf[osdbufpos++] = 0;
}

// write a null-terminated string <s> to the OSD buffer starting at line <n>
void OsdWriteOffset(unsigned char n, const char *s, unsigned char invert, unsigned char stipple, char offset, char leftchar, char usebg, int maxinv, int mininv)
{
	//printf("OsdWriteOffset(%d)\n", n);
	unsigned short i;
	unsigned char b;
	const unsigned char *p;
	unsigned char stipplemask = 0xff;
	int linelimit = OSDLINELEN;
	int arrowmask = arrow;
	int palette = 0;
	if (n == (osd_size-1) && (arrow & OSD_ARROW_RIGHT))
		linelimit -= 22;

	if (n && n < OsdGetSize() - 1) leftchar = 0;

	if (stipple) {
		stipplemask = 0x55;
		stipple = 0xff;
		palette |= 0x2;
	}
	else
		stipple = 0;

	osd_start(n);

	unsigned char xormask = 0;
	unsigned char xorchar = 0;

	i = 0;
	// send all characters in string to OSD
	while (1)
	{
		if (invert && i / 8 >= mininv)
		{
			xormask = 255;
			palette |= 0x1;
		}

		if (invert && i / 8 >= maxinv)
		{
			xormask = 0;
			palette &= ~0x1;
		}

		if (i == 0 && (n < osd_size))
		{	// Render sidestripe
			unsigned char tmp[8];

			if (leftchar)
			{
				unsigned char tmp2[8];
				memcpy(tmp2, charfont[(uint)leftchar], 8);
				rotatechar(tmp2, tmp);
				p = tmp;
			}
			else
			{
				p = &titlebuffer[(osd_size - 1 - n) * 8];
			}

			draw_title(p);
			i += 22;
		}
		else if (n == (osd_size-1) && (arrowmask & OSD_ARROW_LEFT))
		{	// Draw initial arrow
			unsigned char b;

			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			p = &charfont[0x10][0];
			for (b = 0; b<8; b++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
			p = &charfont[0x14][0];
			for (b = 0; b<8; b++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;
			osdbuf[osdbufpos++] = xormask;

			SetTile( i / 8, n, 0, palette, CharToTile(0x10) );
			SetTile( ( i / 8 ) + 1, n, 0, palette, CharToTile(0x14) );

			i += 24;
			arrowmask &= ~OSD_ARROW_LEFT;
			if (*s++ == 0) break;	// Skip 3 characters, to keep alignent the same.
			if (*s++ == 0) break;
			if (*s++ == 0) break;
		}
		else
		{
			b = *s++;
			if (!b) break;

			if (b == 0xb)
			{
				stipplemask ^= 0xAA;
				stipple ^= 0xff;
			}
			else if (b == 0xc)
			{
				xorchar ^= 0xff;
			}
			else if (b == 0x0d || b == 0x0a)
			{  // cariage return / linefeed, go to next line
			   // increment line counter
				if (++n >= linelimit)
					n = 0;

				// send new line number to OSD
				osd_start(n);
			}
			else if (i<(linelimit - 8))
			{  // normal character
				unsigned char c;
				p = &charfont[b][0];
				SetTile(i / 8, n, 0, palette, CharToTile(b));
				for (c = 0; c<8; c++) {
					char bg = usebg ? framebuffer[n][i+c-22] : 0;
					osdbuf[osdbufpos++] = (((*p++ << offset)&stipplemask) ^ xormask ^ xorchar) | bg;
					stipplemask ^= stipple;
				}
				i += 8;
			}
		}
	}

	for (; i < linelimit; i++) // clear end of line
	{
		char bg = usebg ? framebuffer[n][i-22] : 0;
		osdbuf[osdbufpos++] = xormask | bg;
	}

	if (n == (osd_size-1) && (arrowmask & OSD_ARROW_RIGHT))
	{	// Draw final arrow if needed
		unsigned char c;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		p = &charfont[0x15][0];
		for (c = 0; c<8; c++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
		p = &charfont[0x11][0];
		for (c = 0; c<8; c++) osdbuf[osdbufpos++] = (*p++ << offset) ^ xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;
		osdbuf[osdbufpos++] = xormask;

		SetTile( i / 8, n, 0, palette, CharToTile(0x15) );
		SetTile( ( i / 8 ) + 1, n, 0, palette, CharToTile(0x11) );

		i += 22;
	}
}

void OsdShiftDown(unsigned char n)
{
	osd_start(n);

	osdbufpos += 22;
	for (int i = 22; i < 256; i++) osdbuf[osdbufpos++] <<= 1;
}


void OsdDrawLogo(int row)
{
	osd_start(row);

	unsigned char bt = 0;
	const unsigned char *lp = logodata[row];
	int bytes = sizeof(logodata[0]);
	if ((uint)row >= (sizeof(logodata) / sizeof(logodata[0]))) lp = 0;

	char *bg = framebuffer[row];

	int i = 0;
	while(i < OSDLINELEN)
	{
		if (i == 0)
		{
			draw_title(&titlebuffer[(osd_size - 1 - row) * 8]);
			i += 22;
		}

		if(lp && bytes)
		{
			bt = *lp++;
			bytes--;
		}

		osdbuf[osdbufpos++] = bt | *bg++;
		++i;
	}
}

#define INFO_MAXW 32
#define INFO_MAXH 16

void OSD_PrintInfo(const char *message, int *width, int *height, int frame)
{
	static char str[INFO_MAXW * INFO_MAXH];
	memset(str, ' ', sizeof(str));

	// calc height/width if none provided. Add frame to calculated size.
	// no frame will be added if width and height are provided.
	int calc = !*width || !*height || frame;

	int maxw = 0;
	int x = calc ? 1 : 0;
	int y = calc ? 1 : 0;
	while (*message)
	{
		char c = *message++;
		if (c == 0xD) continue;
		if (c == 0xA)
		{
			x = calc ? 1 : 0;
			y++;
			continue;
		}

		if (x < INFO_MAXW && y < INFO_MAXH) str[(y*INFO_MAXW) + x] = c;

		x++;
		if (x > maxw) maxw = x;
	}

	int w = !calc ? *width + 2 : maxw+1;
	if (w > INFO_MAXW) w = INFO_MAXW;
	*width = w;

	int h = !calc ? *height + 2 : y+2;
	if (h > INFO_MAXH) h = INFO_MAXH;
	*height = h;

	if (frame)
	{
		frame = (frame - 1) * 6;
		for (x = 1; x < w - 1; x++)
		{
			str[(0 * INFO_MAXW) + x] = 0x81+frame;
			str[((h - 1)*INFO_MAXW) + x] = 0x81 + frame;
		}
		for (y = 1; y < h - 1; y++)
		{
			str[(y * INFO_MAXW)] = 0x83 + frame;
			str[(y * INFO_MAXW) + w - 1] = 0x83 + frame;
		}
		str[0] = 0x80 + frame;
		str[w - 1] = 0x82 + frame;
		str[(h - 1)*INFO_MAXW] = 0x85 + frame;
		str[((h - 1)*INFO_MAXW) + w - 1] = 0x84 + frame;
	}

	for (y = 0; y < h; y++)
	{
		osd_start(y);

		for (x = 0; x < w; x++)
		{
			const unsigned char *p = charfont[(uint)str[(y*INFO_MAXW) + x]];
			for (int i = 0; i < 8; i++)
			{
				osdbuf[osdbufpos++] = *p;
				SetTile(x, y, 0, 0, CharToTile(*p));
				p++;
			}
		}
	}
}

// clear OSD frame buffer
void OsdClear(void)
{
	osdset = -1;
	memset(osdbuf, 0, 16 * 256);
	ClearTileLayer(0, 0);
	ClearTileLayer(1, 0);
}

// enable displaying of OSD
void OsdEnable(unsigned char mode)
{
	user_io_osd_key_enable(mode & DISABLE_KEYBOARD);
	mode &= (DISABLE_KEYBOARD | OSD_MSG);
	spi_osd_cmd(OSD_CMD_ENABLE | mode);
}

void InfoEnable(int x, int y, int width, int height)
{
	user_io_osd_key_enable(0);
	spi_osd_cmd_cont(OSD_CMD_ENABLE | OSD_INFO);
	spi_w(x);
	spi_w(y);
	spi_w(width);
	spi_w(height);
	DisableOsd();
}

void OsdRotation(uint8_t rotate)
{
	spi_osd_cmd_cont(OSD_CMD_DISABLE);
	spi_w(0);
	spi_w(0);
	spi_w(0);
	spi_w(0);
	spi_w(rotate);
	DisableOsd();
}

// disable displaying of OSD
void OsdDisable()
{
	user_io_osd_key_enable(0);
	spi_osd_cmd(OSD_CMD_DISABLE);
}

void OsdMenuCtl(int en)
{
	if (en)
	{
		spi_osd_cmd(OSD_CMD_WRITE | 8);
		spi_osd_cmd(OSD_CMD_ENABLE);
	}
	else
	{
		spi_osd_cmd(OSD_CMD_DISABLE);
	}
}

// write a null-terminated string <s> to the OSD buffer starting at line <n>
static void print_line(unsigned char line, const char *hdr, const char *text, unsigned long width, unsigned long offset, unsigned char invert)
{
	// line : OSD line number (0-7)
	// text : pointer to null-terminated string
	// start : start position (in pixels)
	// width : printed text length in pixels
	// offset : scroll offset in pixels counting from the start of the string (0-7)
	// invert : invertion flag

	const unsigned char *p;

	if (invert) invert = 0xff;

	// select buffer and line to write to
	osd_start(line);
	draw_title(&titlebuffer[(osd_size - 1 - line) * 8]);

	while (*hdr)
	{
		width -= 8;
		p = charfont[(uint)(*hdr++)];
		for (int i = 0; i < 8; i++) osdbuf[osdbufpos++] = *p++ ^ invert;
	}

	if (offset)
	{
		width -= 8 - offset;
		p = &charfont[(uint)(*text++)][offset];
		for (; offset < 8; offset++) osdbuf[osdbufpos++] = *p++ ^ invert;
	}

	while (width > 8)
	{
		unsigned char b;
		p = &charfont[(uint)(*text++)][0];
		for (b = 0; b < 8; b++) osdbuf[osdbufpos++] = *p++ ^ invert;
		width -= 8;
	}

	if (width)
	{
		p = &charfont[(uint)(*text++)][0];
		while (width--) osdbuf[osdbufpos++] = *p++ ^ invert;
	}
}

void ScrollText(char n, const char *str, int off, int len, int max_len, unsigned char invert, int idx)
{
	// this function is called periodically when a string longer than the window is displayed.

#define BLANKSPACE 10 // number of spaces between the end and start of repeated name

	char s[40], hdr[40];
	long offset;
	if (!max_len) max_len = 30;

	if (str && str[0] && CheckTimer(scroll_timer[idx])) // scroll if long name and timer delay elapsed
	{
		hdr[0] = 0;
		if (off)
		{
			strncpy(hdr, str, off);
			hdr[off] = 0;
			str += off;
			if (len > off) len -= off;
		}

		scroll_timer[idx] = GetTimer(SCROLL_DELAY2); // reset scroll timer to repeat delay

		scroll_offset[idx]++; // increase scroll position (1 pixel unit)
		memset(s, ' ', 32); // clear buffer

		if (!len) len = strlen(str); // get name length

		if (off+2+len > max_len) // scroll name if longer than display size
		{
			// reset scroll position if it exceeds predefined maximum
			if (scroll_offset[idx] >= (uint)(len + BLANKSPACE) << 3) scroll_offset[idx] = 0;

			offset = scroll_offset[idx] >> 3; // get new starting character of the name (scroll_offset is no longer in 2 pixel unit)
			len -= offset; // remaining number of characters in the name
			if (len>max_len) len = max_len;
			if (len > 0) strncpy(s, &str[offset], len); // copy name substring

			if (len < max_len - BLANKSPACE) // file name substring and blank space is shorter than display line size
			{
				strncpy(s + len + BLANKSPACE, str, max_len - len - BLANKSPACE); // repeat the name after its end and predefined number of blank space
			}

			print_line(n, hdr, s, (max_len - 1) << 3, (scroll_offset[idx] & 0x7), invert); // OSD print function with pixel precision
		}
	}
}

void ScrollReset(int idx)
{
	scroll_timer[idx] = GetTimer(SCROLL_DELAY); // set timer to start name scrolling after predefined time delay
	scroll_offset[idx] = 0; // start scrolling from the start
}

/* core currently loaded */
static char lastcorename[261 + 10] = "CORE";
void OsdCoreNameSet(const char* str)
{
	sprintf(lastcorename, "%s", str);
}

char* OsdCoreNameGet()
{
	return lastcorename;
}

void OsdUpdate()
{
	TileInit();

	int n = is_menu() ? 19 : osd_size;
	for (int i = 0; i < n; i++)
	{
		if (osdset & (1 << i))
		{
			spi_osd_cmd_cont(OSD_CMD_WRITE | i);
			spi_write(osdbuf + i * 256, 256, 0);
			DisableOsd();
			if (is_megacd()) mcd_poll();
			if (is_pce()) pcecd_poll();
		}
	}

	if( palette_dirty )
	{
		spi_osd_cmd_cont(OSD_CMD_PALETTE);
		spi_write( (uint8_t *)tile_palette, sizeof(tile_palette), 1);
		DisableOsd();
		palette_dirty = false;
	}

/*	const char *str = "Hello World!";

	const char *p = str;
	int idx = 0;
	while( *p )
	{
		tile_map[idx] = CharToTile( *p );
		p++;
		idx++;
	}
*/
	spi_osd_cmd_cont(OSD_CMD_TILEDATA);
	spi_w( 0 );
	spi_write( tile_data, sizeof(tile_data), 0);
	DisableOsd();

	memset( tile_inuse_prev, 0, sizeof( tile_inuse_prev ) );
	for( int pos = 0; pos < ( TILE_MAP_HEIGHT * TILE_MAP_WIDTH * TILE_MAP_NUM_LAYERS ); pos++ )
	{
		const uint16_t tile = tile_map[pos];
		const uint8_t tile_idx = tile & 0x7f;
		tile_inuse_prev[ tile_idx >> 5 ] |= tile_idx & 0x1f;
		if( tile & ( 1 << 15 ) )
		{
			EnableOsd();
			spi_b(OSD_CMD_TILEMAP);
			spi_w(pos);
			spi_w(tile);
			DisableOsd();
			tile_map[pos] &= ~( 1 << 15 );
		}
	}
	osdset = 0;
}
