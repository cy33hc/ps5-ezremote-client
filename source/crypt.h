#include <wolfssl/openssl/conf.h>
#include <wolfssl/openssl/evp.h>
#include <wolfssl/openssl/err.h>

#ifdef __cplusplus
extern "C"
{
#endif

int openssl_encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key,
            unsigned char *iv, unsigned char *ciphertext, int *ciphertext_len);

int openssl_decrypt(unsigned char *ciphertext, int ciphertext_len, unsigned char *key,
            unsigned char *iv, unsigned char *plaintext, int *plaintext_len);

#ifdef __cplusplus
}
#endif