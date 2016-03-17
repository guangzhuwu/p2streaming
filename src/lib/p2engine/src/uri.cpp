//
// url.ipp
// ~~~~~~~
//
// Copyright (c) 2009 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#ifdef _MSC_VER
#	define _SCL_SECURE_NO_WARNINGS//disable WARNINGS
#endif

#include "p2engine/uri.hpp"

#include <cstring>
#include <cctype>
#include <cstdlib>

#include <vector>

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

namespace p2engine {

	void tokenize(const std::string& src, const std::string& tok, 
		std::vector<std::string>& v, 
		bool trim = false, const std::string& null_subst = "")
	{
		typedef std::string::size_type size_type;
		if (src.empty() || tok.empty())
			return;
		size_type pre_index = 0, index = 0, len = 0;
		while ((index = src.find_first_of(tok, pre_index)) != std::string::npos)
		{
			if ((len = index - pre_index) != 0)
			{
				v.push_back(std::string());
				v.back().assign(&src[pre_index], len);
			}
			else if (trim == false)
			{
				v.push_back(null_subst);
			}
			pre_index = index + 1;
		}
		std::string endstr = src.substr(pre_index);
		if (trim == false)
			v.push_back(endstr.empty() ? null_subst : endstr);
		else if (!endstr.empty())
			v.push_back(endstr);
	}

	void uri::clear()
	{
		query_map_.clear();
		protocol_.clear();
		user_info_.clear();
		user_name_.clear();
		user_password_.clear();
		host_.clear();
		port_.clear();
		path_.clear();
		query_.clear();
		fragment_.clear();
		ipv6_host_ = false;
	}

	unsigned short uri::port() const
	{
		if (!port_.empty())
			return (unsigned short)std::atoi(port_.c_str());
		else if (protocol_ == "ftp")
			return 21;
		else if (protocol_ == "ssh")
			return 22;
		else if (protocol_ == "telnet")
			return 23;
		else if (protocol_ == "tftp")
			return 69;
		else if (protocol_ == "http")
			return 80;
		else if (protocol_ == "nntp")
			return 119;
		else if (protocol_ == "ldap")
			return 389;
		else if (protocol_ == "https")
			return 443;
		else if (protocol_ == "ftps")
			return 990;

		return 0;
	}

	std::string uri::to_string(int components) const
	{
		std::string s;

		if ((components & protocol_component) != 0 && !protocol_.empty())
		{
			s = protocol_;
			s += "://";
		}

		if ((components & user_info_component) != 0 && !user_info_.empty())
		{
			s += user_info_;
			s += "@";
		}

		if ((components & host_component) != 0)
		{
			if (ipv6_host_)
				s += "[";
			s += host_;
			if (ipv6_host_)
				s += "]";
		}

		if ((components & port_component) != 0 && !port_.empty())
		{
			s += ":";
			s += port_;
		}

		if ((components & path_component) != 0 && !path_.empty())
		{
			s += path_;
		}

		if ((components & query_component) != 0 && !query_.empty())
		{
			s += "?";
			s += query_;
		}

		if ((components & fragment_component) != 0 && !fragment_.empty())
		{
			s += "#";
			s += fragment_;
		}

		return s;
	}

	bool uri::from_string(const char* p, error_code& ec)
	{
		clear();
		from_string(*this, p, ec);
		return !!ec;
	}

	void uri::from_string(uri& new_url, const char* p, error_code& ec)
	{
		std::string str = normalize(p);
		const char* s = str.c_str();

		// Protocol.
		std::size_t length = std::strcspn(s, ":");
		if (length + 2 <= str.length() &&
			(s[length] == ':') && (s[length + 1] == '/') && (s[length + 2] == '/'))
		{
			new_url.protocol_.assign(s, s + length);
			boost::trim_if(new_url.protocol_, ::isspace);

			//boost::to_lower(new_url.protocol_);
			//std::local has a bug in msvc stl.It will caush crash. so, ...
			for (size_t i = 0; i<new_url.protocol_.size(); ++i)
				new_url.protocol_[i] = ::tolower(new_url.protocol_[i]);
			s += length;

			// "://".
			if (*s++ != ':')
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
			if (*s++ != '/')
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
			if (*s++ != '/')
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
		}
		else
		{
			new_url.protocol_ = "";
		}

		// UserInfo.
		length = std::strcspn(s, "@:[/?#");
		if (s[length] == '@')
		{
			new_url.user_info_.assign(s, s + length);
			s += length + 1;
		}
		else if (s[length] == ':')
		{
			std::size_t length2 = std::strcspn(s + length, "@/?#");
			if (s[length + length2] == '@')
			{
				new_url.user_info_.assign(s, s + length + length2);
				s += length + length2 + 1;
			}
		}
		if (!new_url.user_info_.empty())
		{
			parse_user_info(new_url.user_info_, new_url.user_name_, new_url.user_password_);
		}

		// Host.
		if (*s == '[')
		{
			length = std::strcspn(++s, "]");
			if (s[length] != ']')
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
			new_url.host_.assign(s, s + length);
			new_url.ipv6_host_ = true;
			s += length + 1;
			if (std::strcspn(s, ":/?#") != 0)
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
		}
		else
		{
			length = std::strcspn(s, ":/?#");
			new_url.host_.assign(s, s + length);
			s += length;
		}

		// Port.
		if (*s == ':')
		{
			length = std::strcspn(++s, "/?#");
			if (length == 0)
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
			new_url.port_.assign(s, s + length);
			for (std::size_t i = 0; i < new_url.port_.length(); ++i)
			{
				if (!::isdigit((unsigned char)new_url.port_[i]))
				{
					ec = make_error_code(boost::system::errc::invalid_argument);
					return;
				}
			}
			s += length;
		}

		// Path.
		if (*s == '/')
		{
			length = std::strcspn(s, "?#");
			new_url.path_.assign(s, s + length);
			std::string tmp_path;
			if (!unescape_path(new_url.path_, tmp_path))
			{
				ec = make_error_code(boost::system::errc::invalid_argument);
				return;
			}
			s += length;
		}
		else
			new_url.path_ = "/";

		// Query.
		if (*s == '?')
		{
			ec = parse_query(++s, &new_url.query_, new_url.query_map_);
			if (ec) return;
		}

		// Fragment.
		if (*s == '#')
			new_url.fragment_.assign(++s);

		ec = error_code();
	}

	bool uri::from_string(const char* s)
	{
		error_code ec;
		uri new_url;
		new_url.from_string(s, ec);
		if (ec)
		{
			boost::system::system_error ex(ec);
			boost::throw_exception(ex);
		}
		return !!ec;
	}

	bool uri::unescape_path(const std::string& in, std::string& out)
	{
		out.clear();
		out.reserve(in.size());
		for (std::size_t i = 0; i < in.size(); ++i)
		{
			switch (in[i])
			{
			case '%':
				if (i + 3 <= in.size())
				{
					unsigned int value = 0;
					for (std::size_t j = i + 1; j < i + 3; ++j)
					{
						char c = in[j];
						if (c >= '0' && c <= '9')
							value += c - '0';
						else if (c >= 'a' && c <= 'f')
							value += c - 'a' + 10;
						else if (c >= 'A' && c <= 'F')
							value += c - 'A' + 10;
						else
							return false;
						if (j == i + 1)
							value <<= 4;
					}
					out += static_cast<char>(value);
					i += 2;
				}
				else
					return false;
				break;

			default:
				if (!is_uri_char(in[i]))
					return false;
				out += in[i];
				break;
			}
		}
		return true;
	}

	void uri::parse_user_info(const std::string&userInfo, std::string& userName, std::string& pwd)
	{
		userName.clear();
		pwd.clear();
		if (!userInfo.empty())
		{
			std::string::size_type n = userInfo.find(':');
			if (n != std::string::npos)
			{
				userName = userInfo.substr(0, n);
				if (n<userInfo.length())
					pwd = userInfo.substr(n + 1, userInfo.length() - n);
			}
		}
	}
	void uri::combine_user_info(const std::string& userName, const std::string& pwd, std::string&userInfo)
	{
		if (pwd.length() + userInfo.length() != 0)
		{
			userInfo.reserve((pwd.length() + userInfo.length()) + 1);
			if (!userName.empty())
			{
				userInfo = userName;
			}
			if (!pwd.empty())
			{
				userInfo += ":";
				userInfo + pwd;
			}
		}
		else
		{
			userInfo.clear();
		}
	}
	error_code uri::combine_query(const std::map<std::string, std::string>& qmap, std::string& q)
	{
		int i = 0;
		for (std::map<std::string, std::string>::const_iterator itr = qmap.begin(); itr != qmap.end(); ++itr)
		{
			if (i>0)
				q += '&';
			q += itr->first;
			q += '='; q += itr->second;
			++i;
		}
		return error_code();
	}

	error_code uri::parse_query(const char* q, std::string*outQuery, std::map<std::string, std::string>& qmap)
	{
		const char* p = q;
		size_t length = std::strcspn(p, "#");
		std::string goodQuery;
		if (length != strlen(q))
		{
			goodQuery.assign(p, p + length);
			p = goodQuery.c_str();
		}
		std::vector<std::string> result;
		std::vector<std::string> result2;
		boost::split(result, p, boost::is_any_of("&"));
		for (std::size_t i = 0; i<result.size(); ++i)
		{
			p = result[i].c_str();
			result2.clear();
			boost::split(result2, p, boost::is_any_of("="));
			if (result2.size()>0)
			{
				if (result2.size() >= 2)
				{
					std::string tem_res;
					if (!unescape_path(result2[1], tem_res))
					{
						return  make_error_code(boost::system::errc::invalid_argument);
					}
					qmap[result2[0]] = tem_res;
				}
				else
					qmap[result2[0]] = "";
			}
		}
		if (outQuery)
		{
			if (!goodQuery.empty())
				outQuery->swap(goodQuery);
			else
				outQuery->assign(q, q + length);
		}

		return error_code();
	}


	bool operator==(const uri& a, const uri& b)
	{
		return a.protocol_ == b.protocol_
			&& a.user_info_ == b.user_info_
			&& a.host_ == b.host_
			&& a.port_ == b.port_
			&& a.path_ == b.path_
			&& a.query_ == b.query_
			&& a.fragment_ == b.fragment_;
	}

	bool operator<(const uri& a, const uri& b)
	{
#define COMPARE_(x)\
		if (a.x < b.x) return true; \
		if (b.x < a.x) return false;

		COMPARE_(protocol_);
		COMPARE_(user_info_);
		COMPARE_(host_);
		COMPARE_(port_);
		COMPARE_(path_);
		COMPARE_(query_);
		COMPARE_(fragment_);
#undef COMPARE_
		return false;
	}

	std::string uri::normalize(std::string const& in)
	{
		return normalize(in.c_str(), in.length());
	}

	std::string uri::normalize(const char* in, size_t len)
	{
		std::string inPath(in, len);

		//trim space
		boost::trim_if(inPath, ::isspace);

		int header_slash = 0;
		for (size_t i = 0; i<inPath.size() && inPath[i] == '\\'; ++i)
			header_slash++;

		//replace"\\" with "/"
		boost::replace_all(inPath, "\\", "/");
		//replace"//" with "/", but "://" will not be replaced
		boost::replace_first(inPath, ":/", ":\\");//prevent "://" be replaced
		for (;;)
		{
			boost::replace_all(inPath, "//", "/");
			if (inPath.length() == len)
				break;
			else
				len = inPath.length();
		}

		//is there a "/" at the end?
		bool append_slash = (inPath.size() > 1 && inPath[inPath.size() - 1] == '/') ? 1 : 0;

		// this loop removes leading '/..' and trailing '/.' elements.
		std::vector<std::string> tokens;
		std::vector<std::string*> remainder;
		tokens.reserve(8);
		tokenize(inPath, "/", tokens, false);
		remainder.reserve(tokens.size());
		for (size_t i = 0; i< tokens.size(); ++i)
		{
			if (".." == tokens[i])
			{
				if (!remainder.empty())
					remainder.pop_back();
				else
					remainder.push_back(&tokens[i]);
			}
			else if ("." != tokens[i])
			{
				remainder.push_back(&tokens[i]);
			}
		}
		inPath.clear();
		for (size_t i = 0; i<remainder.size(); ++i)
		{
			inPath += *remainder[i];
			if (i == remainder.size() - 1)
				break;
			inPath += "/";
		}
		boost::replace_first(inPath, ":\\", ":/");

		//for "\\\\pipe\xxx"
		if (header_slash>0)
			inPath.insert(0, header_slash, '\\');
		//for "c:\\a\b\c"
		if (inPath.size() >= 3 && memcmp(&inPath[1], "://", 3) == 0)
			boost::replace_first(inPath, "://", ":/");
		if (append_slash && (inPath.empty() || inPath[inPath.length() - 1] != '/'))
			return inPath + '/';

		return inPath;
	}

	/*
	* escape URL
	*
	* Puts escape codes in URLs.  (More complete than it used to be;
	* GN Jan 1997.  We escape all that isn't alphanumeric, "safe" or "extra"
	* as spec'd in RFCs 1738, 1808 and 2068.)
	*/
	bool uri::is_uri_char(int c)
	{
		static const char unreserved_chars[] =
			// when determining if a url needs encoding
			// % should be ok
			"%+"
			// reserved
			";?:@=&, $/"
			// unreserved (special characters) ' excluded, 
			// since some buggy trackers fail with those
			"-_!.~*()";

		unsigned int uc = (unsigned int)c;
		if (uc > 128)
			return false;
		if (::isalnum(uc))
			return true;
		if (strchr(unreserved_chars, uc))
			return true;
		return false;
	}

	bool uri::is_escaped(const std::string& s)
	{
		//const char* unsafe = " <>#{}|\\^~[]`";
		bool has_only_url_chars = true;
		bool has_percent = false;
		for (size_t n = 0; n < s.length(); ++n)
		{
			char c = s[n];
			if (c == '%')
				has_percent = true;
			else if (!is_uri_char(c))
				has_only_url_chars = false;
		}
		return has_percent && has_only_url_chars;
	}

	std::string uri::escape(const char *src, bool space_to_plus)
	{
		static const char *hex = "0123456789ABCDEF";

		std::string buffer;
		buffer.reserve(2 * strlen(src));

		for (const char *cp = src; *cp; cp++)
		{
			if (*cp == ' ' && space_to_plus)
				buffer += ('+');
			else if (*cp == '\\')
				buffer += ('/');
			else if (is_uri_char((unsigned char)*cp) || (*cp == '+' && !space_to_plus))
			{
				buffer += (*cp);
			}
			else
			{
				buffer += ('%');
				buffer += (hex[(unsigned char)*cp / 16]);
				buffer += (hex[(unsigned char)*cp % 16]);
			}
		}

		return buffer;
	}

} // namespace url

