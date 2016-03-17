#ifndef shunt_typedef_h__
#define shunt_typedef_h__

#include <string>
#include <set>

#include <boost/type_traits/is_reference.hpp>

namespace p2shunt
{
	namespace detail{
		template <typename _Type, int>
		struct type_wrapper
		{
			BOOST_STATIC_ASSERT(!boost::is_reference<_Type>::value);
			typedef _Type type;
			type		  value;

			type_wrapper(){}
			explicit type_wrapper(const type& v){value=v;}
		};
		enum{HOST_TYPE, USER_NAME_TYPE, PWD_TYPE, DB_NAME_TYPE, 
			SHUNT_ID_TYPE, SHUNT_NAME_TYPE};
	};


	//为了避免这些同类型的参数传递顺序错误，分别定义类型。
	typedef detail::type_wrapper<std::string, detail::HOST_TYPE>		host_name_t;
	typedef detail::type_wrapper<std::string, detail::USER_NAME_TYPE>	user_name_t;
	typedef detail::type_wrapper<std::string, detail::PWD_TYPE>			password_t;
	typedef detail::type_wrapper<std::string, detail::DB_NAME_TYPE>		db_name_t;
	typedef detail::type_wrapper<std::string, detail::SHUNT_ID_TYPE>	shunt_id_t;
	typedef detail::type_wrapper<std::string, detail::SHUNT_NAME_TYPE>	shunt_name_t;

	struct shunt_xml_param
	{	
		shunt_xml_param(){}

		explicit shunt_xml_param(const shunt_id_t& shuntID)
			:id(shuntID.value){}

		shunt_xml_param(const shunt_id_t& shuntID, const shunt_name_t& shuntName)
			:id(shuntID.value), name(shuntName.value){}

		shunt_id_t::type		id;
		shunt_name_t::type		name;
		std::string				receive_url;
		std::set<std::string>	send_urls;
	};
}
;
#endif // typedef_h__
