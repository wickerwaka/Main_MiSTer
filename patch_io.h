#ifndef PATCH_IO_H
#define PATCH_IO_H 1

#include "file_io.h"

struct PatchCmdVec;

enum class PatchType
{
    Invalid = 0,
    IPS,
    BPS
};

struct PatchIO
{
    PatchIO();
    ~PatchIO();

    fileTYPE *source_file;

    PatchType patch_type;
    __off64_t source_size;
    __off64_t target_size;

    uint8_t *patch_data;
    uint32_t patch_data_size;

    PatchCmdVec *cmds;
};


bool PatchIO_Open(PatchIO *patch_io, fileTYPE *source_file, const char *patch_path);
__off64_t PatchIO_GetTargetSize(const PatchIO *patch_io);
int PatchIO_Read(PatchIO *patch_io, __off64_t read_offset, int length, void *buffer);





#endif // PATCH_IO_H