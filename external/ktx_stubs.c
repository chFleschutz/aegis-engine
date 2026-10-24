// libktx refers to zlib supercompression and to the KTX1 reader, which external/ktx leaves out (see
// tools/update-ktx.sh): the engine writes and reads KTX2 files with zstd only. These stand in for them and report
// the feature unsupported, which libktx passes on as an error.

#include <ktx.h>

#include "ktxint.h"
#include "texture1.h"

ktx_size_t ktxCompressZLIBBounds(ktx_size_t srcLength)
{
    (void)srcLength;
    return 0;
}

KTX_error_code ktxCompressZLIBInt(unsigned char* pDest, ktx_size_t* pDestLength, const unsigned char* pSrc, ktx_size_t srcLength,
    ktx_uint32_t level)
{
    (void)pDest;
    (void)pDestLength;
    (void)pSrc;
    (void)srcLength;
    (void)level;
    return KTX_UNSUPPORTED_FEATURE;
}

KTX_error_code ktxUncompressZLIBInt(unsigned char* pDest, ktx_size_t* pDestLength, const unsigned char* pSrc, ktx_size_t srcLength)
{
    (void)pDest;
    (void)pDestLength;
    (void)pSrc;
    (void)srcLength;
    return KTX_UNSUPPORTED_FEATURE;
}

KTX_error_code ktxTexture1_constructFromStreamAndHeader(ktxTexture1* This, ktxStream* pStream, KTX_header* pHeader,
    ktxTextureCreateFlags createFlags)
{
    (void)This;
    (void)pStream;
    (void)pHeader;
    (void)createFlags;
    return KTX_UNSUPPORTED_FEATURE;
}
