#include "rsa_rc4.h"
#include <openssl/rsa.h>
#include <openssl/rc4.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "p2engine/safe_buffer_io.hpp"

#ifdef BOOST_MSVC
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#endif

using namespace p2engine;
namespace ppc{
	/*KSA*/
	void rc4::rc4_init(unsigned char* key, int32_t key_length)
	{
		for (size_t i=0; i<KEY_SIZE; ++i)
			seed_[i] = i;

		for (size_t i=0, j=0; i<KEY_SIZE; ++i)
		{
			j = (j + key[i%key_length] + seed_[i])& 255;
			std::swap(seed_[i], seed_[j]);
		}
	}

	void rc4::rc4_crypt(const unsigned char s[rc4::KEY_SIZE], 
		unsigned long key_len, 
		char *Data, unsigned long Len)
	{
		//RC4并非两个字符串简单的xor，不能使用fast_xor！
		int x = 0, y = 0, t = 0;
		for(unsigned long i=0;i<Len;i++)
		{
			x=(x+1)%key_len;
			y=(y+s[x])%key_len;
			t=(s[x]+s[y])%key_len;
			Data[i] ^= s[t];
		}
	}

	void rc4::rc4_decrypt(const unsigned char s[KEY_SIZE], 
		unsigned long key_len, 
		char *Data, unsigned long Len)
	{
		rc4_crypt(s, key_len, Data, Len);
	}

	bool evp_decrypt(const RSA* const rsa_key, 
		const char* encrypt_data, int encrypt_data_len, 
		const char* ek, int ek_len, safe_buffer& result)
	{
		safe_buffer_io io(&result);
		io.clear();
		io.prepare(encrypt_data_len);
		unsigned char* pptrOfDe=(unsigned char*) io.pptr();
		int outl = encrypt_data_len;
		int total = encrypt_data_len;

		char iv[8];
		EVP_CIPHER_CTX ctx2;
		EVP_PKEY *pubkey = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(pubkey, rsa_key);
		EVP_CIPHER_CTX_init(&ctx2);
		int ret=EVP_OpenInit(&ctx2, EVP_rc4(), (const unsigned char*)ek, ek_len, 
			(unsigned char*)iv, pubkey);
		if(ret!=1)
			return false;
		ret = EVP_OpenUpdate(&ctx2, pptrOfDe, &outl, 
			(const unsigned char*)encrypt_data, total);
		if(ret!=1)
			return false;
		ret=EVP_OpenFinal(&ctx2, (pptrOfDe+outl), &outl);
		if(ret!=1)
			return false;
		total += outl;
		io.commit(total);
		return true;
	}

	safe_buffer get_decrypt_data(const std::string& data_content, 
		const RSA* const rsa_key)
	{
		//前4个字节是xor_key的length
		if (data_content.length()>=2*sizeof(uint32_t))
		{
			const char* ptr=data_content.c_str();
			BOOST_STATIC_ASSERT(rc4::SIZE_TGA==sizeof(uint32_t));
			uint32_t key_size=read_uint32_ntoh(ptr);
			if (data_content.size()>=(key_size+rc4::SIZE_TGA))
			{
				int dataLen=int(data_content.size()-key_size-rc4::SIZE_TGA);
				safe_buffer result;
				if(evp_decrypt(rsa_key, ptr+key_size, dataLen, ptr, key_size, result))
					return result;
			}
		}

		//不是rc4格式，
		safe_buffer result;
		safe_buffer_io io(&result);
		io<<data_content;

		return result;
	}

};
