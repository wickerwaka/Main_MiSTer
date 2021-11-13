#if !defined(TILES_H)
#define TILES_H 1

#include <stdint.h>

#define NUM_TILES 128

#define TILE_CHAR_START 0
#define TILE_CHAR_END 64
#define TILE_GFX_START 64
#define TILE_GFX_END 96
#define TILE_DYNAMIC_START 96
#define TILE_DYNAMIC_END 128

#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define TILE_MAP_WIDTH 33
#define TILE_MAP_HEIGHT 32
#define TILE_MAP_NUM_LAYERS 2
#define TILE_PLANES 4

#define TILE_PLANE_0 0x0
#define TILE_PLANE_1 0x1
#define TILE_PLANE_2 0x2
#define TILE_PLANE_3 0x3
#define TILE_PLANE_01 0x3
#define TILE_PLANE_23 0x5
#define TILE_PLANE_0123 0x6

struct TileRef
{
	uint16_t tile_idx: 7;
	uint16_t plane : 3;
	uint16_t _padding : 6;
};

void Tiles_Init();
void Tiles_Update();
void Tiles_LoadGfxTiles( const char *path );

TileRef Tiles_CharRef( int c );
void Tiles_SetTile( int x, int y, int layer, int palette, TileRef tile_ref );
void Tiles_ClearRow( int y, int layer, int palette );
void Tiles_ClearLayer( int layer, int palette );

#endif // TILES_H