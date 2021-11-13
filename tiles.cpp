#include "charrom.h"
#include "tiles.h"
#include "spi.h"

#include "lib/lodepng/lodepng.h"

#define OSD_CMD_PALETTE  0x60      // OSD palette
#define OSD_CMD_TILEDATA 0x80      // OSD tile pixel data
#define OSD_CMD_TILEMAP  0xc0      // OSD tile map
#define OSD_CMD_TILESCROLL  0xe0      // OSD tile scroll

union TileMapEntry
{
	struct
	{
		uint16_t tile_idx: 7;
		uint16_t plane : 3;
		uint16_t palette : 3;
		uint16_t _padding : 2;
		uint16_t modified : 1;
	};
	uint16_t u16;
};

static uint8_t tile_data[TILE_PLANES][NUM_TILES * TILE_HEIGHT];
static TileMapEntry tile_map[TILE_MAP_WIDTH * TILE_MAP_HEIGHT * TILE_MAP_NUM_LAYERS];
static uint16_t tile_palette[32];
static uint16_t default_tile_palette[32] = {
	0x200, 0xfdd, // Normal
	0x200, 0x686, // Disabled
	0x200, 0xf88, // Red
	0x200, 0x8f8, // Green
	0xfdd, 0x200, // Inverted Normal
	0x686, 0x200, // Inverted Disabled
	0xf88, 0x200, // Inverted Red
	0x8f8, 0x200, // Inverted Green

	0x000, 0x235, 0x825, 0x051, // pico-8 palette
	0xb53, 0x655, 0xccc, 0xffe,
	0xf05, 0xfa0, 0xff2, 0x0e3,
	0x3bf, 0x87a, 0xf7a, 0xfdb,
};
static bool palette_dirty;

static void CharToTileData(int c, TileRef tile_ref);
static void ChunkyToPlaner(int w, int h, const uint8_t* pixels, uint8_t first_tile);

void Tiles_Init()
{
	static bool inited = false;

	if( inited )
    {
        return;
    }

    inited = true;
    memcpy(tile_palette, default_tile_palette, sizeof(default_tile_palette));
    palette_dirty = true;

    for( int i = 0; i < 256; i++ )
    {
        CharToTileData( i, Tiles_CharRef(i) );
    }

    Tiles_LoadGfxTiles( "/media/fat/tiles.png" );
}

void Tiles_LoadGfxTiles( const char *path )
{
    unsigned char *png_data;
    unsigned char *pixel_data;

    size_t png_size;
    unsigned int w, h;

    int result;

    result = lodepng_load_file(&png_data, &png_size, path);

    if( result )
    {
        printf( "Failed to load %s: %d\n", path, result );
        return;
    }

    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    result = lodepng_decode(&pixel_data, &w, &h, &state, png_data, png_size);

    free( png_data );

    if( result )
    {
        printf( "Failed to decode PNG %s: %d\n", path, result );
        return;
    }

    unsigned int align_w = ( ( w + 7 ) / 8 ) * 8;
    unsigned int pixel_count = ( TILE_GFX_END - TILE_GFX_START ) * 8 * 8;
    unsigned int rows = ( pixel_count ) / align_w;

    ChunkyToPlaner( align_w, rows > h ? h : rows, pixel_data, TILE_GFX_START );

    for( int c = 0; c < 16; c++ )
    {
        unsigned char r = state.info_png.color.palette[( c * 4 ) + 0] >> 4;
        unsigned char g = state.info_png.color.palette[( c * 4 ) + 1] >> 4;
        unsigned char b = state.info_png.color.palette[( c * 4 ) + 2] >> 4;
        tile_palette[c + 16] = ( r << 8 ) | ( g << 4 ) | ( b << 0 );
    }
    palette_dirty = true;

    free( pixel_data );

    lodepng_state_cleanup(&state);
}

TileRef Tiles_CharRef( int c )
{
	TileRef ref;
	ref.plane = (c & 0x3);
	ref.tile_idx = ( c >> 2 ) + TILE_CHAR_START;

	return ref;
}


void Tiles_SetTile( int x, int y, int layer, int palette, TileRef tile_ref )
{
	if( x < 0 || x >= TILE_MAP_WIDTH )
		return;

	if( y < 0 || y >= TILE_MAP_HEIGHT )
		return;

	if( layer < 0 || layer >= TILE_MAP_NUM_LAYERS )
		return;
	
	int pos = y + ( layer * TILE_MAP_HEIGHT ) + ( x * TILE_MAP_NUM_LAYERS * TILE_MAP_HEIGHT );
	TileMapEntry *entry = &tile_map[pos];
	entry->modified = 1;
	entry->palette = palette;
	entry->plane = tile_ref.plane;
	entry->tile_idx = tile_ref.tile_idx;
}

void Tiles_ClearRow( int y, int layer, int palette )
{
	TileRef ref;
	ref.tile_idx = 0;
	ref.plane = TILE_PLANE_0;

	for( int x = 0; x < TILE_MAP_WIDTH; x++ )
	{
		Tiles_SetTile(x, y, layer, palette, ref );
	}
}

void Tiles_ClearLayer( int layer, int palette )
{
	if( layer < 0 || layer >= TILE_MAP_NUM_LAYERS )
		return;
	
	for( int y = 0; y < TILE_MAP_HEIGHT; y++ )
		Tiles_ClearRow( y, layer, palette );
}

void Tiles_Update()
{
    if( palette_dirty )
	{
		spi_osd_cmd_cont(OSD_CMD_PALETTE);
		spi_write( (uint8_t *)tile_palette, sizeof(tile_palette), 1);
		DisableOsd();
		palette_dirty = false;
	}

	for( int plane = 0; plane < TILE_PLANES; plane++ )
	{
		uint16_t loc = plane << 14;
		spi_osd_cmd_cont(OSD_CMD_TILEDATA);
		spi_w( loc );
		spi_write( tile_data[plane], sizeof(tile_data[0]), 0);
		DisableOsd();
	}

    for( int i = 0; i < 8; i++ )
    {
	    TileRef ref;
	    ref.tile_idx = 64 + i;
	    ref.plane = TILE_PLANE_0123;
	    Tiles_SetTile( 0, i, 0, 1, ref );
	    Tiles_SetTile( 5, i, 1, 1, ref );
    }


	for( int pos = 0; pos < ( TILE_MAP_HEIGHT * TILE_MAP_WIDTH * TILE_MAP_NUM_LAYERS ); pos++ )
	{
		TileMapEntry* entry = &tile_map[pos];
		if( entry->modified )
		{
			EnableOsd();
			spi_b(OSD_CMD_TILEMAP);
			spi_w(pos);
			spi_w(entry->u16);
			DisableOsd();
			entry->modified = 0;
		}
	}

	spi_osd_cmd_cont(OSD_CMD_TILESCROLL);
	spi_w( 0 );
	for( int y = 0; y < 32; y++ )
		spi_w(0);
	DisableOsd();
}

// Convert the 4bpp palettized `pixels` data into tile data
// pixel data is byte sized, but only the lowest 4-bits are used
// w and h are assumed to be multiples of 8, undefined behavior if they aren't
static void ChunkyToPlaner(int w, int h, const uint8_t* pixels, uint8_t first_tile)
{
	uint8_t *planes[4];
	for( int p = 0; p < 4; p++ )
	{
		planes[p] = tile_data[p] + ( first_tile * 8 );
	}

	for( int cy = 0; cy < h; cy += 8 )
	{
		for( int cx = 0; cx < w; cx += 8 )
		{
			for( int y = 0; y < 8; y++ )
			{
				const uint8_t *pixel = pixels + ( ( cy + y ) * w ) + cx;
				for( int p = 0; p < 4; p++ )
				{
					const uint8_t mask = 1 << p;
					*planes[p] = 0;
					for( int x = 0; x < 8; x++ )
					{
						uint8_t c = pixel[x];
						*planes[p] |= ( c & mask ) ? ( 1 << x ) : 0;
					}
					planes[p]++;
				}
			}
		}
	}
}

static void CharToTileData(int c, TileRef tile_ref)
{
	uint8_t *p = &charfont[c][0];
	uint8_t *t = &tile_data[tile_ref.plane][tile_ref.tile_idx * 8];
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
}

