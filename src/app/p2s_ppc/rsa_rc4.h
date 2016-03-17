#ifndef _PPC_RSA_RC4_
#define _PPC_RSA_RC4_

#include "p2engine/safe_buffer.hpp"
#include <openssl/ossl_typ.h>

namespace ppc{
	using namespace p2engine;

	struct rc4{
		enum{SIZE_TGA=4};
		enum{KEY_SIZE=256};

		/*KSA*/
		void rc4_init(unsigned char* key, int32_t key_length);

		void rc4_crypt(char *Data,  unsigned long Len){
			rc4_crypt(seed_, KEY_SIZE, Data, Len);
		}

		void rc4_decrypt(char *Data, unsigned long Len){
			rc4_decrypt(seed_, KEY_SIZE, Data, Len);
		}

		static void rc4_crypt(const unsigned char s[KEY_SIZE], unsigned long key_len,  
			                  char *Data,  unsigned long Len);

		static void rc4_decrypt(const unsigned char s[KEY_SIZE], unsigned long key_len, 
			                    char *Data, unsigned long Len);

	private:
		unsigned char seed_[KEY_SIZE];
	};

	bool evp_decrypt(const RSA* const rsa_key, const char* encrypt_data, int encrypt_data_len, 
		             const char* ek, int ek_len, safe_buffer& result);

	safe_buffer get_decrypt_data(const std::string& data_content, const RSA* const rsa_key);

}
#endif //_PPC_RSA_RC4_
