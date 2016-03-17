/*

Copyright (c) 2003, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef P2ENGINE_ESCAPE_STRING_HPP_INCLUDED
#define P2ENGINE_ESCAPE_STRING_HPP_INCLUDED

#include "p2engine/push_warning_option.hpp"
#include <string>
#include <boost/limits.hpp>
#include <boost/array.hpp>
#include "p2engine/pop_warning_option.hpp"

#include "p2engine/config.hpp"
#include "libupnp/utility.h"
#include "libupnp/error_code.hpp"

namespace libupnp
{
	 boost::array<char, 3 + std::numeric_limits<size_type>::digits10> to_string(size_type n);
	 bool is_alpha(char c);
	 bool is_digit(char c);
	 bool is_print(char c);
	 bool is_space(char c);
	 char to_lower(char c);

	 int split_string(char const** tags, int buf_size, char* in);
	 bool string_begins_no_case(char const* s1, char const* s2);
	 bool string_equal_no_case(char const* s1, char const* s2);

	 void url_random(char* begin, char* end);

	 std::string unescape_string(std::string const& s, error_code& ec);
	// replaces all disallowed URL characters by their %-encoding
	 std::string escape_string(const char* str, int len);
	// same as escape_string but does not encode '/'
	 std::string escape_path(const char* str, int len);
	// if the url does not appear to be encoded, and it contains illegal url characters
	// it will be encoded
	 std::string maybe_url_encode(std::string const& url);

	 bool need_encoding(char const* str, int len);

	// encodes a string using the base64 scheme
	 std::string base64encode(std::string const& s);
	 std::string base64decode(std::string const& s);
	// encodes a string using the base32 scheme
	 std::string base32encode(std::string const& s);
	 std::string base32decode(std::string const& s);

	 std::string url_has_argument(
		std::string const& url, std::string argument, std::string::size_type* out_pos = 0);

	// replaces \ with /
	 void convert_path_to_posix(std::string& path);

	 std::string read_until(char const*& str, char delim, char const* end);
	 std::string to_hex(std::string const& s);
	 bool is_hex(char const *in, int len);
	 void to_hex(char const *in, int len, char* out);
	 bool from_hex(char const *in, int len, char* out);
}

#endif // P2ENGINE_ESCAPE_STRING_HPP_INCLUDED

