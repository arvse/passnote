/* ------------------------------------------------------------------
 * Pass Note - Utility Functions
 * ------------------------------------------------------------------ */

#include "util.h"

void secure_free_mem ( void *mem, size_t size )
{
    memset ( mem, '\0', size );
    free ( mem );
}

void secure_free_string ( char *string )
{
    if ( string )
    {
        secure_free_mem ( string, strlen ( string ) + 1 );
    }
}

int random_bytes ( void *buffer, size_t length )
{
    int fd;
    size_t sum;
    size_t len;

    if ( ( fd = open ( "/dev/random", O_RDONLY ) ) < 0 )
    {
        return -1;
    }

    for ( sum = 0; sum < length; sum += len )
    {
        if ( ( len = read ( fd, ( ( unsigned char * ) buffer ) + sum, length - sum ) ) <= 0 )
        {
            close ( fd );
            return -1;
        }
    }

    close ( fd );
    return 0;
}
