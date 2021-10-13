#include <string.h>
#include "patch_io.h"

#include <algorithm>

static const uint8_t IPS_START[] = { 0x50, 0x41, 0x54, 0x43, 0x48 };
static const uint8_t IPS_EOF[] = { 0x45, 0x4f, 0x46 };

enum class PatchCmdType
{
    IPSData,
    IPSRLE,
};

struct PatchCmdIPSData
{
    const uint8_t *data;
};

struct PatchCmdIPSRLE
{
    uint8_t byte;
};

struct PatchCmd
{
    uint32_t offset;
    uint32_t length;
    PatchCmdType type;
    union
    {
        PatchCmdIPSData ips_data;
        PatchCmdIPSRLE ips_rle;
    };
};

struct PatchCmdVec
{
    PatchCmd *vec;
    size_t size;
    size_t capacity;

    PatchCmdVec()
    {
        size = 0;
        capacity = 16;
        vec = new PatchCmd[capacity];
    }

    ~PatchCmdVec()
    {
        delete [] vec;
    }

    PatchCmd *push()
    {
        if( size == capacity )
        {
            capacity *= 2;
            PatchCmd *newvec = new PatchCmd[capacity];
            memcpy(newvec, vec, sizeof(PatchCmd) * size);
            delete [] vec;
            vec = newvec;
        }

        size += 1;
        return vec + size - 1;
    }

    PatchCmd *begin() { return vec; }
    PatchCmd *end() { return vec + size; }
};

struct Decoder
{
    const uint8_t *data;
    const uint32_t size;

    uint32_t pos;

    Decoder( const uint8_t *data, uint32_t size )
        : data(data), size(size), pos(0) {}

    
    bool eof() const
    {
        return pos >= size;
    }
    
    bool match( const uint8_t *match, uint32_t match_len )
    {
        if( ( pos + match_len ) > size ) return false;

        if( !memcmp( match, data + pos, match_len ) )
        {
            pos += match_len;
            return true;
        }

        return false;
    }

    uint32_t ips3()
    {
        if( pos + 3 > size )
        {
            pos = size;
            return 0;
        }

        uint32_t r = data[pos+0] << 16 | data[pos+1] << 8 | data[pos+2];
        pos += 3;
        return r;
    }

    uint32_t ips2()
    {
        if( pos + 2 > size )
        {
            pos = size;
            return 0;
        }

        uint32_t r = data[pos+0] << 8 | data[pos+1];
        pos += 2;
        return r;
    }

    uint8_t byte()
    {
        if( pos + 1 > size )
        {
            pos = size;
            return 0;
        }

        uint8_t r = data[pos];
        pos += 1;
        return r;
    }

    bool skip(uint32_t s)
    {
        if( pos + s > size )
        {
            pos = size;
            return false;
        }
        pos += s;
        return true;
    }

    const uint8_t* ptr() const
    {
        return data + pos;
    }
};


PatchIO::PatchIO()
{
    patch_type = PatchType::Invalid;
    patch_data = nullptr;
    source_file = nullptr;
    
    cmds = nullptr;
}

PatchIO::~PatchIO()
{
    delete [] patch_data;
    delete cmds;
}

bool PatchIO_Open(PatchIO *patch_io, fileTYPE *source_file, const char *patch_path)
{
    fileTYPE patch_file;

    if( !FileOpen( &patch_file, patch_path) )
    {
        return false;
    }

    patch_io->patch_data_size = FileGetSize( &patch_file );
    patch_io->patch_data = new uint8_t [ patch_io->patch_data_size ];
    if( FileReadAdv( &patch_file, patch_io->patch_data, patch_io->patch_data_size ) != (int)patch_io->patch_data_size )
    {
        return false;
    }
    FileClose(&patch_file);

    Decoder dec(patch_io->patch_data, patch_io->patch_data_size);

    if( !dec.match( IPS_START, sizeof( IPS_START ) ) )
    {
        printf( "Not an IPS file\n");
        return false;
    }

    patch_io->patch_type = PatchType::IPS;

    PatchCmdVec *pvec = new PatchCmdVec();
    patch_io->cmds = pvec;

    PatchCmd dummy;

    while( !dec.match( IPS_EOF, sizeof( IPS_EOF)) )
    {
        PatchCmd *cmd = &dummy;
        uint32_t offset = dec.ips3();
        if( offset >= 512 )
        {
            cmd = pvec->push();
        }
        
        cmd->offset = offset - 512;
        uint32_t size = dec.ips2();
        if( size == 0 )
        {
            cmd->type = PatchCmdType::IPSRLE;
            cmd->length = dec.ips2();
            cmd->ips_rle.byte = dec.byte();
        }
        else
        {
            cmd->type = PatchCmdType::IPSData;
            cmd->ips_data.data = dec.ptr();
            cmd->length = size;
            dec.skip(size);
        }

        if( dec.eof() )
        {
            printf( "Unexpected EOF\n");
            break;
        }
    }

    std::sort(pvec->begin(), pvec->end(), [](const PatchCmd& a, const PatchCmd& b) { return a.offset < b.offset; });

    PatchCmd *last_cmd = &pvec->vec[pvec->size - 1];

    __off64_t patch_size = last_cmd->offset + last_cmd->length;
    __off64_t source_size = FileGetSize( source_file );

    patch_io->source_file = source_file;
    patch_io->target_size = patch_size > source_size ? patch_size : source_size;
    patch_io->source_size = source_size;

    return true;
}


__off64_t PatchIO_GetTargetSize(const PatchIO *patch_io)
{
    return patch_io->target_size;
}

static void ApplyCmd(PatchCmd *cmd, uint32_t begin, uint32_t end, uint8_t *buffer)
{
    uint32_t apply_begin = cmd->offset < begin ? begin : cmd->offset;
    uint32_t apply_end = cmd->offset + cmd->length > end ? end : cmd->offset + cmd->length;

    if( apply_begin >= apply_end )
    {
        return;
    }

    const uint32_t apply_offset = apply_begin - begin;
    const uint32_t apply_length = apply_end - apply_begin;
    const uint32_t cmd_offset = cmd->offset < begin ? begin - cmd->offset : 0;

    switch( cmd->type )
    {
        case PatchCmdType::IPSRLE:
            memset( buffer + apply_offset, cmd->ips_rle.byte, apply_length );
            break;
        
        case PatchCmdType::IPSData:
            memcpy( buffer + apply_offset, cmd->ips_data.data + cmd_offset, apply_length );
            break;
    }
}

int PatchIO_Read(PatchIO *patch_io, __off64_t read_offset, int length, void *buffer)
{
    __off64_t end_offset = read_offset + length;
    if( end_offset > patch_io->target_size )
    {
        end_offset = patch_io->target_size;
    }

    __off64_t disk_end_offset = end_offset < patch_io->source_size ? end_offset : patch_io->source_size;

    memset( buffer, 0, end_offset - read_offset );

    if( read_offset < disk_end_offset )
    {
        FileSeek( patch_io->source_file, read_offset, SEEK_SET ); // TODO error
        FileReadAdv( patch_io->source_file, buffer, disk_end_offset - read_offset );
    }

    PatchCmdVec *pvec = patch_io->cmds;

    PatchCmd *end_cmd = std::upper_bound( pvec->begin(), pvec->end(), end_offset, []( __off64_t pos, const PatchCmd& cmd ) {
        return pos < cmd.offset;
    } );

    if( end_cmd == pvec->begin() )
    {
        return end_offset - read_offset;
    }

    PatchCmd *start_cmd = end_cmd - 1;
    while( start_cmd >= pvec->begin() )
    {
        if( start_cmd->offset + start_cmd->length < read_offset )
        {
            break;
        }
        start_cmd -= 1;
    }
    start_cmd += 1;

    if( start_cmd == end_cmd )
    {
        return end_offset - read_offset; // todo real value - nothing found
    }

    PatchCmd *cmd = start_cmd;
    while( cmd != end_cmd )
    {
        ApplyCmd( cmd, read_offset, end_offset, (uint8_t *)buffer);
        cmd++;
    }

    return end_offset - read_offset;
}