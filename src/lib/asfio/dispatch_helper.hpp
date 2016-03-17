#ifndef __ASFILE_DSK_CACHE_HELPER_HPP__
#define __ASFILE_DSK_CACHE_HELPER_HPP__


#include <p2engine/p2engine.hpp>

namespace asfio{

	using namespace p2engine;

	class backstage_running_base
		:public basic_engine_object
		, public running_service<1>
		, public operation_mark
	{
	protected:
		backstage_running_base(io_service& ios)
			:basic_engine_object(ios)
		{
		}
	};

	template<typename DoHandler, typename Callback>
	struct disk_cache_helper
		:handler_allocator
	{
		typedef typename backstage_running_base::op_stamp_t op_stamp_t;
		typedef DoHandler do_handler_type;
		typedef Callback  callback_handler_type;

		backstage_running_base* cache_impl;
		DoHandler do_handler;
		Callback  callback_handler;
		op_stamp_t stamp;

		disk_cache_helper(backstage_running_base& cachImpl, 
			const DoHandler& doHandler, const Callback& callbackHandler)
			:cache_impl(&cachImpl), do_handler(doHandler)
			, callback_handler(callbackHandler), stamp(cachImpl.op_stamp())
		{
		}

	};

	template<typename DoOpenHandler, typename OpenCallback>
	struct open_helper
		: disk_cache_helper<DoOpenHandler, OpenCallback>
	{
		open_helper(backstage_running_base& cachImpl, 
			const DoOpenHandler& doOpen, const OpenCallback& openCallback
			)
			:disk_cache_helper<DoOpenHandler, OpenCallback>(cachImpl, doOpen, 
			openCallback)
		{
		}

		void operator ()()
		{
			if (this->cache_impl->is_canceled_op(this->stamp))
				return;

			p2engine::error_code ec;
			this->do_handler(ec);

			operation_mark_dispatch(this->cache_impl, 
				boost::protect(boost::bind(this->callback_handler, ec)), 
				&this->stamp);
		}
	};

	template<typename DoOpenHandler, typename OpenCallback>
	void dispatch_open_helper(backstage_running_base& cachImpl, 
		const DoOpenHandler& doOpen, const OpenCallback& openCallback
		)
	{
		cachImpl.get_running_io_service().dispatch(
			open_helper<DoOpenHandler, OpenCallback>(cachImpl, doOpen, openCallback)
			);
	}

	template<typename DoReadHandler, typename ReadCallback>
	struct read_piece_helper
		:disk_cache_helper<DoReadHandler, ReadCallback>
	{
		read_piece_helper(backstage_running_base& cachImpl, 
			const DoReadHandler& doRead, const ReadCallback& readCallback
			)
			:disk_cache_helper<DoReadHandler, ReadCallback>(cachImpl, doRead, 
			readCallback)
		{
		}
		void operator ()()
		{
			if (this->cache_impl->is_canceled_op(this->stamp))
				return;

			p2engine::error_code ec;
			safe_buffer buf;
			this->do_handler(buf, ec);

			operation_mark_dispatch(this->cache_impl, 
				boost::protect(boost::bind(this->callback_handler, buf, ec)), 
				&this->stamp);
		}
	};

	template<typename DoReadHandler, typename ReadCallback>
	void dispatch_read_piece_helper(backstage_running_base& cachImpl, 
		const DoReadHandler& doRead, const ReadCallback& readCallback
		)
	{
		cachImpl.get_running_io_service().dispatch(
			read_piece_helper<DoReadHandler, ReadCallback>(cachImpl, doRead, readCallback)
			);
	}

	template<typename DoWriteHandler, typename WriteCallback>
	struct write_piece_helper
		:disk_cache_helper<DoWriteHandler, WriteCallback>
	{
		write_piece_helper(backstage_running_base& cachImpl, 
			const DoWriteHandler& doWrite, const WriteCallback& writeCallback
			)
			:disk_cache_helper<DoWriteHandler, WriteCallback>(cachImpl, doWrite, writeCallback)
		{
		}

		void operator ()()
		{
			if (this->cache_impl->is_canceled_op(this->stamp))
				return;

			p2engine::error_code ec;
			std::size_t len;
			this->do_handler(len, ec);

			operation_mark_dispatch(this->cache_impl, 
				boost::protect(boost::bind(this->callback_handler, len, ec)), 
				&this->stamp);
		}
	};

	template<typename DoWriteHandler, typename WriteCallback>
	void dispatch_write_piece_helper(
		backstage_running_base& cachImpl, 
		const DoWriteHandler& doWrite, const WriteCallback& writeCallback
		)
	{
		cachImpl.get_running_io_service().dispatch(
			write_piece_helper<DoWriteHandler, WriteCallback>(cachImpl, doWrite, writeCallback)
			);
	}

}

#endif//__ASFILE_DSK_CACHE_HELPER_HPP__
