#ifndef __CURVE25519_H__
#define __CURVE25519_H__

#ifdef __cplusplus
extern "C" 
{
#endif

int curve25519(void *mypublic, const void*secret, const void *basepoint);

#ifdef __cplusplus
}
#endif

#endif//__CURVE25519_H__
