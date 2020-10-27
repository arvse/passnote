/* ------------------------------------------------------------------
 * Pass Note - Database Storage
 * ------------------------------------------------------------------ */

#include "storage.h"
#include "util.h"
#include <lz4.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>

#define AES256_KEYLEN 32
#define AES256_KEYLEN_BITS (AES256_KEYLEN*8)
#define AES256_BLOCKLEN 16
#define SHA256_BLOCKLEN 32
#define DERIVE_N_ROUNDS 50000

static int pbkdf2_sha256_derive_key ( const char *password, const uint8_t * salt, size_t salt_len,
    uint8_t * key, size_t key_size )
{
    mbedtls_md_context_t sha256_ctx;
    const mbedtls_md_info_t *sha256_info;

    mbedtls_md_init ( &sha256_ctx );

    if ( !( sha256_info = mbedtls_md_info_from_type ( MBEDTLS_MD_SHA256 ) ) )
    {
        mbedtls_md_free ( &sha256_ctx );
        return -1;
    }

    if ( mbedtls_md_setup ( &sha256_ctx, sha256_info, TRUE ) != 0 )
    {
        mbedtls_md_free ( &sha256_ctx );
        return -1;
    }

    if ( mbedtls_pkcs5_pbkdf2_hmac ( &sha256_ctx, ( const uint8_t * ) password,
            strlen ( password ), salt, salt_len, DERIVE_N_ROUNDS, key_size, key ) != 0 )
    {
        memset ( key, '\0', key_size );
        mbedtls_md_free ( &sha256_ctx );
        return -1;
    }

    mbedtls_md_free ( &sha256_ctx );
    return 0;
}

static int hmac_sha256 ( const uint8_t * key, size_t key_len, const uint8_t * input, size_t length,
    uint8_t * hash )
{
    mbedtls_md_context_t md_ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init ( &md_ctx );

    if ( mbedtls_md_setup ( &md_ctx, mbedtls_md_info_from_type ( md_type ), TRUE ) != 0
        || mbedtls_md_hmac_starts ( &md_ctx, key, key_len ) != 0
        || mbedtls_md_hmac_update ( &md_ctx, input, length ) != 0
        || mbedtls_md_hmac_finish ( &md_ctx, hash ) != 0 )
    {
        mbedtls_md_free ( &md_ctx );
        return -1;
    }

    mbedtls_md_free ( &md_ctx );
    return 0;
}

static int aes256_cbc_encrypt ( const uint8_t * key, const uint8_t * iv, size_t len,
    const uint8_t * src, uint8_t * dst )
{
    int ret = 0;
    mbedtls_aes_context aes;
    uint8_t iv_workbuf[AES256_BLOCKLEN];

    mbedtls_aes_init ( &aes );
    memcpy ( iv_workbuf, iv, AES256_BLOCKLEN );

    if ( mbedtls_aes_setkey_enc ( &aes, key, AES256_KEYLEN_BITS ) != 0
        || mbedtls_aes_crypt_cbc ( &aes, MBEDTLS_AES_ENCRYPT, len, iv_workbuf, src, dst ) != 0 )
    {
        ret = -1;
    }

    mbedtls_aes_free ( &aes );
    memset ( &aes, '\0', sizeof ( aes ) );

    return ret;
}

static int aes256_cbc_decrypt ( const uint8_t * key, const uint8_t * iv, size_t len,
    const uint8_t * src, uint8_t * dst )
{
    int ret = 0;
    mbedtls_aes_context aes;
    uint8_t iv_workbuf[AES256_BLOCKLEN];

    mbedtls_aes_init ( &aes );
    memcpy ( iv_workbuf, iv, AES256_BLOCKLEN );

    if ( mbedtls_aes_setkey_dec ( &aes, key, AES256_KEYLEN_BITS ) != 0
        || mbedtls_aes_crypt_cbc ( &aes, MBEDTLS_AES_DECRYPT, len, iv_workbuf, src, dst ) != 0 )
    {
        ret = -1;
    }

    mbedtls_aes_free ( &aes );
    memset ( &aes, '\0', sizeof ( aes ) );

    return ret;
}

static int read_complete ( int fd, uint8_t * mem, size_t total )
{
    size_t len;
    size_t sum;

    for ( sum = 0; sum < total; sum += len )
    {
        if ( ( ssize_t ) ( len = read ( fd, mem + sum, total - sum ) ) <= 0 )
        {
            return -1;
        }
    }

    return 0;
}

static int write_complete ( int fd, const uint8_t * mem, size_t total )
{
    size_t len;
    size_t sum;

    for ( sum = 0; sum < total; sum += len )
    {
        if ( ( ssize_t ) ( len = write ( fd, mem + sum, total - sum ) ) <= 0 )
        {
            return -1;
        }
    }

    return 0;
}

static struct node_t *load_database_in ( int fd, const char *password )
{
    size_t plaintext_size;
    size_t plaintext_len;
    size_t compressed_len;
    size_t encrypted_len;
    uint8_t *plaintext;
    uint8_t *compressed;
    uint8_t *encrypted;
    struct node_t *result;
    uint8_t salt[AES256_KEYLEN];
    uint8_t key[AES256_KEYLEN];
    uint8_t iv[AES256_BLOCKLEN];
    uint8_t hmac[SHA256_BLOCKLEN];
    uint8_t hmac_calc[SHA256_BLOCKLEN];

    if ( ( off_t ) ( encrypted_len = lseek ( fd, 0, SEEK_END ) ) < 0 )
    {
        return NULL;
    }

    if ( lseek ( fd, 0, SEEK_SET ) < 0 )
    {
        return NULL;
    }

    if ( !password )
    {
        if ( !( encrypted = ( uint8_t * ) malloc ( encrypted_len ) ) )
        {
            return NULL;
        }

        if ( read_complete ( fd, encrypted, encrypted_len ) < 0 )
        {
            secure_free_mem ( encrypted, encrypted_len );
            return NULL;
        }

        result = unpack_tree ( encrypted, encrypted_len );
        secure_free_mem ( encrypted, encrypted_len );
        return result;
    }

    if ( encrypted_len < sizeof ( salt ) + sizeof ( hmac ) + sizeof ( iv ) + AES256_BLOCKLEN )
    {
        return NULL;
    }

    encrypted_len -= sizeof ( salt ) + sizeof ( hmac ) + sizeof ( iv );

    if ( encrypted_len % AES256_BLOCKLEN
        || !( encrypted = ( uint8_t * ) malloc ( encrypted_len ) ) )
    {
        return NULL;
    }

    if ( !( compressed = ( uint8_t * ) malloc ( encrypted_len ) ) )
    {
        free ( encrypted );
        return NULL;
    }

    if ( read_complete ( fd, salt, sizeof ( salt ) ) < 0
        || read_complete ( fd, hmac, sizeof ( hmac ) ) < 0
        || read_complete ( fd, iv, sizeof ( iv ) ) < 0
        || pbkdf2_sha256_derive_key ( password, salt, sizeof ( salt ), key, sizeof ( key ) ) < 0
        || read_complete ( fd, encrypted, encrypted_len ) < 0
        || hmac_sha256 ( key, sizeof ( key ), encrypted, encrypted_len, hmac_calc ) < 0
        || memcmp ( hmac_calc, hmac, SHA256_BLOCKLEN ) )
    {
        memset ( key, '\0', sizeof ( key ) );
        free ( compressed );
        free ( encrypted );
        return 0;
    }

    if ( aes256_cbc_decrypt ( key, iv, encrypted_len, encrypted, compressed ) < 0 )
    {
        memset ( key, '\0', sizeof ( key ) );
        free ( encrypted );
        secure_free_mem ( compressed, encrypted_len );
        return NULL;
    }

    memset ( key, '\0', sizeof ( key ) );
    free ( encrypted );

    compressed_len = encrypted_len;

    if ( salt[0] & 0xf0 )
    {
        compressed_len += ( ( salt[0] & 0xf0 ) >> 4 ) - AES256_BLOCKLEN;
    }

    plaintext_size = encrypted_len * 8;

    if ( !( plaintext = ( uint8_t * ) malloc ( plaintext_size ) ) )
    {
        secure_free_mem ( compressed, encrypted_len );
        return NULL;
    }

    if ( ( ssize_t ) ( plaintext_len =
            LZ4_decompress_safe ( ( char * ) compressed, ( char * ) plaintext, compressed_len,
                plaintext_size ) ) < 0 )
    {
        secure_free_mem ( plaintext, plaintext_len );

        plaintext_size = encrypted_len * 255;

        if ( !( plaintext = ( uint8_t * ) malloc ( plaintext_size ) ) )
        {
            secure_free_mem ( compressed, encrypted_len );
            return NULL;
        }

        if ( ( ssize_t ) ( plaintext_len =
                LZ4_decompress_safe ( ( char * ) compressed, ( char * ) plaintext, compressed_len,
                    plaintext_size ) ) < 0 )
        {
            secure_free_mem ( compressed, encrypted_len );
            secure_free_mem ( plaintext, plaintext_len );
        }
    }

    secure_free_mem ( compressed, encrypted_len );

    result = unpack_tree ( plaintext, plaintext_len );
    secure_free_mem ( plaintext, plaintext_len );

    return result;
}

struct node_t *load_database ( const char *path, const char *password )
{
    int fd;
    struct node_t *database;

    if ( ( fd = open ( path, O_RDONLY ) ) < 0 )
    {
        return NULL;
    }

    database = load_database_in ( fd, password[0] ? password : NULL );
    close ( fd );
    return database;
}

static int save_database_in ( int fd, const uint8_t * plaintext, size_t len, const char *password )
{
    size_t compressed_size;
    size_t compressed_len;
    uint8_t *compressed;
    uint8_t *encrypted;
    uint8_t salt[AES256_KEYLEN];
    uint8_t key[AES256_KEYLEN];
    uint8_t iv[AES256_BLOCKLEN];
    uint8_t hmac[SHA256_BLOCKLEN];

    if ( !password )
    {
        return write_complete ( fd, plaintext, len ) < 0 ? -1 : 0;
    }

    if ( random_bytes ( salt, sizeof ( salt ) ) < 0
        || random_bytes ( iv, sizeof ( iv ) ) < 0
        || ( ssize_t ) ( compressed_size = LZ4_compressBound ( len ) ) < 0 )
    {
        return -1;
    }

    compressed_size += AES256_BLOCKLEN;

    if ( !( compressed = ( uint8_t * ) malloc ( compressed_size ) ) )
    {
        return -1;
    }

    if ( ( ssize_t ) ( compressed_len = LZ4_compress_default ( ( const char * ) plaintext,
                ( char * ) compressed, len, compressed_size ) ) < 0 )
    {
        secure_free_mem ( compressed, compressed_size );
        return -1;
    }

    salt[0] = ( ( compressed_len % AES256_BLOCKLEN ) << 4 ) | ( salt[0] & 0x0f );

    if ( pbkdf2_sha256_derive_key ( password, salt, sizeof ( salt ), key, sizeof ( key ) ) < 0 )
    {
        secure_free_mem ( compressed, compressed_size );
        return -1;
    }

    while ( compressed_len % AES256_BLOCKLEN )
    {
        compressed[compressed_len++] = '\0';
    }

    if ( !( encrypted = ( uint8_t * ) malloc ( compressed_len ) ) )
    {
        memset ( key, '\0', sizeof ( key ) );
        secure_free_mem ( compressed, compressed_size );
        return -1;
    }

    if ( aes256_cbc_encrypt ( key, iv, compressed_len, compressed, encrypted ) < 0 )
    {
        memset ( key, '\0', sizeof ( key ) );
        secure_free_mem ( compressed, compressed_size );
        free ( encrypted );
        return -1;
    }

    if ( hmac_sha256 ( key, sizeof ( key ), encrypted, compressed_len, hmac ) < 0 )
    {
        memset ( key, '\0', sizeof ( key ) );
        secure_free_mem ( compressed, compressed_size );
        free ( encrypted );
        return -1;
    }

    memset ( key, '\0', sizeof ( key ) );
    secure_free_mem ( compressed, compressed_size );

    if ( write_complete ( fd, salt, sizeof ( salt ) ) < 0
        || write_complete ( fd, hmac, sizeof ( hmac ) ) < 0
        || write_complete ( fd, iv, sizeof ( iv ) ) < 0
        || write_complete ( fd, encrypted, compressed_len ) < 0 )
    {
        free ( encrypted );
        return -1;
    }

    free ( encrypted );
    return 0;
}

int save_database ( const char *path, struct node_t *node, const char *password )
{
    int ret;
    int fd;
    size_t packed_len;
    uint8_t *packed;
    char backup_path[PATH_SIZE];

    if ( pack_tree ( node, &packed, &packed_len ) < 0 )
    {
        return -1;
    }

    snprintf ( backup_path, sizeof ( backup_path ), "%s.bak", path );
    rename ( path, backup_path );

    if ( ( fd = open ( path, O_CREAT | O_TRUNC | O_WRONLY, 0644 ) ) < 0 )
    {
        secure_free_mem ( packed, packed_len );
        return -1;
    }

    ret = save_database_in ( fd, packed, packed_len, password[0] ? password : NULL );

    secure_free_mem ( packed, packed_len );
    syncfs ( fd );
    close ( fd );
    return ret;
}

char *read_plain_file ( const char *path )
{
    int fd;
    size_t total;
    char *content;

    if ( ( fd = open ( path, O_RDONLY ) ) < 0 )
    {
        return NULL;
    }

    if ( ( off_t ) ( total = lseek ( fd, 0, SEEK_END ) ) < 0 )
    {
        close ( fd );
        return NULL;
    }

    if ( lseek ( fd, 0, SEEK_SET ) < 0 )
    {
        close ( fd );
        return NULL;
    }

    if ( !( content = ( char * ) malloc ( total + 1 ) ) )
    {
        close ( fd );
        return NULL;
    }

    if ( read_complete ( fd, ( uint8_t * ) content, total ) < 0 )
    {
        secure_free_mem ( content, total );
        close ( fd );
        return NULL;
    }
    content[total] = '\0';

    close ( fd );
    return content;
}

int write_plain_file ( const char *path, const char *content )
{
    int fd;

    if ( ( fd = open ( path, O_CREAT | O_TRUNC | O_WRONLY, 0644 ) ) < 0 )
    {
        return -1;
    }

    if ( write_complete ( fd, ( uint8_t * ) content, strlen ( content ) ) < 0 )
    {
        close ( fd );
        return -1;
    }

    close ( fd );
    return 0;
}
