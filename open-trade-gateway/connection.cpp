/////////////////////////////////////////////////////////////////////////
///@file connection.cpp
///@brief	websocket连接类
///@copyright	上海信易信息科技股份有限公司 版权所有 
/////////////////////////////////////////////////////////////////////////

#include "connection.h"
#include "connection_manager.h"
#include "log.h"
#include "config.h"
#include "SerializerTradeBase.h"
#include "user_process_info.h"

#include <iostream>

using namespace std::chrono;

TUserProcessInfoMap g_userProcessInfoMap;

connection::connection(boost::asio::io_context& ios
	,boost::asio::ip::tcp::socket socket,
	connection_manager& manager,
	int connection_id)
	:m_ios(ios),
	m_ws_socket(std::move(socket)),		
	m_input_buffer(),
	m_output_buffer(),
	connection_manager_(manager),	
	_connection_id(connection_id),	
	_reqLogin(),	
	_user_broker_key(""),
	flat_buffer_(),
	req_(),
	_X_Real_IP(""),
	_X_Real_Port(0),
	_agent(""),
	_analysis(""),
	_msg_cache()
{		
}

void connection::start()
{	
	int fd=m_ws_socket.next_layer().native_handle();
	int flags = fcntl(fd, F_GETFD);
	flags |= FD_CLOEXEC;
	fcntl(fd, F_SETFD, flags);

	req_ = {};
	boost::beast::http::async_read(m_ws_socket.next_layer()
		,flat_buffer_
		,req_,
		boost::beast::bind_front_handler(
			&connection::on_read_header,
			shared_from_this()));	
}

void connection::stop()
{
	try
	{
		Log().WithField("fun","stop")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_INFO,"trade connection stop");		
		m_ws_socket.next_layer().close();
	}
	catch (std::exception& ex)
	{
		Log().WithField("fun","stop")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)			
			.Log(LOG_INFO,"connection stop exception");	
	}
}

void connection::OnOpenConnection(boost::system::error_code ec)
{
	if (ec)
	{
		Log().WithField("fun","OnOpenConnection")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ec.message())
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_WARNING,"trade connection accept fail");	
		OnCloseConnection();
		return;
	}

	try
	{
		boost::asio::ip::tcp::endpoint remote_ep = m_ws_socket.next_layer().remote_endpoint();
		_X_Real_IP = req_["X-Real-IP"].to_string();
	
		if (_X_Real_IP.empty())
		{
			_X_Real_IP = remote_ep.address().to_string();
		}

		std::string real_port = req_["X-Real-Port"].to_string();
		if (real_port.empty())
		{
			_X_Real_Port = remote_ep.port();
		}
		else
		{
			_X_Real_Port = atoi(real_port.c_str());
		}
		
		_agent = req_[boost::beast::http::field::user_agent].to_string();
		_analysis = req_["analysis"].to_string();
				
		SendTextMsg(g_config.broker_list_str);
		DoRead();
	}
	catch (std::exception& ex)
	{
		Log().WithField("fun","OnOpenConnection")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ex.what())
			.Log(LOG_WARNING,"connection OnOpenConnection exception");	
	}	
}

void connection::on_read_header(boost::beast::error_code ec
	, std::size_t bytes_transferred)
{
	boost::ignore_unused(bytes_transferred);

	if (ec == boost::beast::http::error::end_of_stream)
	{
		Log().WithField("fun","on_read_header")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ec.message())
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_WARNING,"connection on_read_header fail");		
		OnCloseConnection();
		return;
	}
	m_ws_socket.async_accept(
		req_,
		boost::beast::bind_front_handler(
			&connection::OnOpenConnection,
			shared_from_this()));
}

void connection::SendTextMsg(const std::string& msg)
{
	try
	{
		if (m_output_buffer.size() > 0)
		{
			m_output_buffer.push_back(msg);
		}
		else
		{
			m_output_buffer.push_back(msg);
			DoWrite();
		}
	}
	catch (std::exception& ex)
	{
		Log().WithField("fun","SendTextMsg")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ex.what())
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_WARNING,"connection SendTextMsg exception");		
	}	
}

void connection::DoWrite()
{
	try
	{
		if (m_output_buffer.empty())
		{
			return;
		}
		auto write_buf = boost::asio::buffer(m_output_buffer.front());
		m_ws_socket.text(true);
		m_ws_socket.async_write(
			write_buf,
			boost::beast::bind_front_handler(
				&connection::OnWrite,
				shared_from_this()));
	}
	catch (std::exception& ex)
	{
		Log().WithField("fun","DoWrite")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ex.what())
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_WARNING,"connection DoWrite exception");		
	}	
}

void connection::DoRead()
{
	m_ws_socket.async_read(
		m_input_buffer,
		boost::beast::bind_front_handler(
			&connection::OnRead,
			shared_from_this()));
}

void connection::OnRead(boost::system::error_code ec, std::size_t bytes_transferred)
{
	if (ec)
	{
		if (ec != boost::beast::websocket::error::closed)
		{
			Log().WithField("fun","OnRead")
				.WithField("key","gateway")
				.WithField("ip",_X_Real_IP)
				.WithField("agent",_agent)
				.WithField("analysis",_analysis)
				.WithField("errmsg",ec.message())
				.WithField("connId",_connection_id)
				.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
				.Log(LOG_WARNING,"trade connection read fail");			
		}
		OnCloseConnection();
		return;
	}
	
	std::string strMsg = boost::beast::buffers_to_string(m_input_buffer.data());
	m_input_buffer.consume(bytes_transferred);
	OnMessage(strMsg);
	DoRead();
}

void connection::OnWrite(boost::system::error_code ec,std::size_t bytes_transferred)
{
	if (ec)
	{
		Log().WithField("fun","OnWrite")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("errmsg",ec.message())
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.Log(LOG_WARNING,"trade server send message fail");		
		OnCloseConnection();
		return;
	}				
	if (m_output_buffer.empty())
	{
		return;
	}
	m_output_buffer.pop_front();
	if (m_output_buffer.size() > 0) 
	{
		DoWrite();
	}
}

void connection::OnMessage(const std::string &json_str)
{
	SerializerTradeBase ss;
	if (!ss.FromString(json_str.c_str()))
	{
		Log().WithField("fun","OnMessage")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)			
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.WithField("msgcontent",json_str)
			.Log(LOG_WARNING, "invalid json str");	
		return;
	}
		
	ReqLogin req;
	ss.ToVar(req);

	if (req.aid == "req_login")
	{
		ProcessLogInMessage(req,json_str);
	}
	else
	{
		ProcessOtherMessage(json_str);
	}
}

void string_replace(std::string &strBig, const std::string &strsrc, const std::string &strdst)
{
	std::string::size_type pos = 0;
	std::string::size_type srclen = strsrc.size();
	std::string::size_type dstlen = strdst.size();
	while ((pos = strBig.find(strsrc, pos)) != std::string::npos)
	{
		strBig.replace(pos, srclen, strdst);
		pos += dstlen;
	}
}

void connection::ProcessLogInMessage(const ReqLogin& req, const std::string &json_str)
{
	_login_msg = json_str;	
	_reqLogin = req;
	auto it = g_config.brokers.find(_reqLogin.bid);
	if (it == g_config.brokers.end())
	{
		Log().WithField("fun","ProcessLogInMessage")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("connId",_connection_id)		
			.WithField("bid",req.bid)
			.Log(LOG_WARNING,"trade server req_login invalid bid");

		std::stringstream ss;
		ss << u8"暂不支持:" << req.bid << u8",请联系该期货公司或快期技术支持人员!";
		OutputNotifySycn(311,ss.str(),"WARNING");
		return;
	}

	_reqLogin.broker = it->second;
	bool flag = false;
	if (_reqLogin.broker.broker_type == "ctp")
	{
		flag = true;
	}
	else if (_reqLogin.broker.broker_type == "ctpse")
	{
		flag = true;
	}
	else if (_reqLogin.broker.broker_type == "sim")
	{
		flag = true;
	}
	else if (_reqLogin.broker.broker_type == "perftest")
	{
		flag = true;
	}
	else
	{
		flag = false;
	}

	if (!flag)
	{
		std::stringstream ss;
		ss << u8"暂不支持:" << req.bid << u8",请联系该期货公司或快期技术支持人员!";
		OutputNotifySycn(311, ss.str(), "WARNING");
		return;
	}

	_reqLogin.client_ip = _X_Real_IP;
	_reqLogin.client_port = _X_Real_Port;
	SerializerTradeBase nss;
	nss.FromVar(_reqLogin);
	nss.ToString(&_login_msg);
	
	std::string strBrokerType = _reqLogin.broker.broker_type;

	if (_user_broker_key.empty())
	{
		//为了支持次席而添加的功能
		if ((!_reqLogin.broker_id.empty()) &&
			(!_reqLogin.front.empty()))
		{
			std::string strFront = _reqLogin.front;
			string_replace(strFront,":", "_");
			string_replace(strFront, "/", "");
			string_replace(strFront,".","_");
			_user_broker_key = strBrokerType + "_" + _reqLogin.bid + "_" 
				+ _reqLogin.user_name+"_"+ _reqLogin.broker_id+"_"+ strFront;
		}
		else
		{
			_user_broker_key = strBrokerType + "_" + _reqLogin.bid + "_" + _reqLogin.user_name;
		}		
	}
	else
	{		
		//防止一个连接进行多个登录
		std::string new_user_broker_key= strBrokerType + "_" + _reqLogin.bid + "_" + _reqLogin.user_name;
		if ((!_reqLogin.broker_id.empty()) &&
			(!_reqLogin.front.empty()))
		{
			std::string strFront = _reqLogin.front;
			string_replace(strFront, ":", "_");
			string_replace(strFront, "/", "");
			string_replace(strFront, ".", "_");
			new_user_broker_key = strBrokerType + "_" + _reqLogin.bid + "_"
				+ _reqLogin.user_name + "_" + _reqLogin.broker_id + "_" + strFront;
		}
		
		Log().WithField("fun","ProcessLogInMessage")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("connId",_connection_id)
			.WithField("oldkey",_user_broker_key)
			.WithField("newkey",new_user_broker_key)
			.Log(LOG_INFO,"get user broker key");	

		if (new_user_broker_key != _user_broker_key)
		{
			auto userIt = g_userProcessInfoMap.find(_user_broker_key);
			if (userIt != g_userProcessInfoMap.end())
			{
				UserProcessInfo_ptr userProcessInfoPtr = userIt->second;
				bool flag = userProcessInfoPtr->ProcessIsRunning();
				if (flag)
				{
					userProcessInfoPtr->NotifyClose(_connection_id);
				}
				if (userProcessInfoPtr->user_connections_.find(_connection_id)
					!= userProcessInfoPtr->user_connections_.end())
				{
					userProcessInfoPtr->user_connections_.erase(_connection_id);
				}
			}
		}

		_user_broker_key = new_user_broker_key;
	}
	
	auto userIt = g_userProcessInfoMap.find(_user_broker_key);
	//如果用户进程没有启动,启动用户进程处理
	if (userIt == g_userProcessInfoMap.end())
	{
		 UserProcessInfo_ptr userProcessInfoPtr = std::make_shared<UserProcessInfo>(m_ios,_user_broker_key,_reqLogin);
		 if (nullptr == userProcessInfoPtr)
		 {
			 Log().WithField("fun","ProcessLogInMessage")
				 .WithField("key","gateway")
				 .WithField("ip",_X_Real_IP)
				 .WithField("agent",_agent)
				 .WithField("analysis",_analysis)
				 .WithField("connId",_connection_id)	
				 .WithField("user_key",_user_broker_key)
				 .Log(LOG_ERROR,"new user process fail");					
			 return;
		 }

		 if (!userProcessInfoPtr->StartProcess())
		 {
			 Log().WithField("fun","ProcessLogInMessage")
				 .WithField("key","gateway")
				 .WithField("ip",_X_Real_IP)
				 .WithField("agent",_agent)
				 .WithField("analysis",_analysis)
				 .WithField("connId",_connection_id)
				 .WithField("user_key",_user_broker_key)
				 .Log(LOG_ERROR,"can not start up user process");					
			 return;
		 }

		 userProcessInfoPtr->user_connections_.insert(
			 std::map<int,connection_ptr>::value_type(
				 _connection_id,shared_from_this()));
		 g_userProcessInfoMap.insert(TUserProcessInfoMap::value_type(
			 _user_broker_key,userProcessInfoPtr));		
		 userProcessInfoPtr->SendMsg(_connection_id,_login_msg);

		 if (!_msg_cache.empty())
		 {
			 for (int i = 0; i < _msg_cache.size(); ++i)
			 {
				 userProcessInfoPtr->SendMsg(_connection_id, _msg_cache[i]);

				 Log().WithField("fun","ProcessLogInMessage")
					 .WithField("key","gateway")
					 .WithField("ip",_X_Real_IP)
					 .WithField("agent",_agent)
					 .WithField("analysis",_analysis)
					 .WithField("connId",_connection_id)
					 .WithField("user_key",_user_broker_key)
					 .Log(LOG_INFO,"connection send cache msg");				 
			 }
			 _msg_cache.clear();
		 }
		 
		 return;
	}
	//如果用户进程已经启动,直接利用
	else
	{
		UserProcessInfo_ptr userProcessInfoPtr = userIt->second;
		//进程是否正常运行
		bool flag = userProcessInfoPtr->ProcessIsRunning();
		if (flag)
		{
			userProcessInfoPtr->user_connections_.insert(
				std::map<int, connection_ptr>::value_type(
					_connection_id, shared_from_this()));
			userProcessInfoPtr->SendMsg(_connection_id,_login_msg);
			if (!_msg_cache.empty())
			{
				for (int i = 0; i < _msg_cache.size(); ++i)
				{
					userProcessInfoPtr->SendMsg(_connection_id, _msg_cache[i]);

					Log().WithField("fun","ProcessLogInMessage")
						.WithField("key","gateway")
						.WithField("ip",_X_Real_IP)
						.WithField("agent",_agent)
						.WithField("analysis",_analysis)
						.WithField("connId",_connection_id)
						.WithField("user_key",_user_broker_key)
						.Log(LOG_INFO,"connection send cache msg");					
				}
				_msg_cache.clear();
			}
			return;
		}
		else
		{
			flag = userProcessInfoPtr->StartProcess();
			if (!flag)
			{
				Log().WithField("fun","ProcessLogInMessage")
					.WithField("key","gateway")
					.WithField("ip",_X_Real_IP)
					.WithField("agent",_agent)
					.WithField("analysis",_analysis)
					.WithField("connId",_connection_id)
					.WithField("user_key",_user_broker_key)
					.Log(LOG_INFO,"can not start up user process");					
				return;
			}
			userProcessInfoPtr->user_connections_.insert(
				std::map<int, connection_ptr>::value_type(
					_connection_id, shared_from_this()));			
			userProcessInfoPtr->SendMsg(_connection_id,_login_msg);
			if (!_msg_cache.empty())
			{
				for (int i = 0; i < _msg_cache.size(); ++i)
				{
					userProcessInfoPtr->SendMsg(_connection_id, _msg_cache[i]);
					Log().WithField("fun","ProcessLogInMessage")
						.WithField("key","gateway")
						.WithField("ip",_X_Real_IP)
						.WithField("agent",_agent)
						.WithField("analysis",_analysis)
						.WithField("connId",_connection_id)
						.WithField("user_key",_user_broker_key)
						.Log(LOG_INFO,"connection send cache");					
				}
				_msg_cache.clear();
			}
			return;
		}
	}
}

void connection::ProcessOtherMessage(const std::string &json_str)
{
	auto userIt = g_userProcessInfoMap.find(_user_broker_key);
	if (userIt == g_userProcessInfoMap.end())
	{
		_msg_cache.push_back(json_str);
		return;
	}

	UserProcessInfo_ptr userProcessInfoPtr = userIt->second;	
	bool flag = userProcessInfoPtr->ProcessIsRunning();
	if (!flag)
	{
		OnCloseConnection();
		return;
	}
		
	userProcessInfoPtr->SendMsg(_connection_id,json_str);
}

void connection::OnCloseConnection()
{	
	try
	{
		Log().WithField("fun","OnCloseConnection")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("connId",_connection_id)
			.WithField("user_key",_user_broker_key)
			.Log(LOG_INFO,"connection close self");
		
		auto userIt = g_userProcessInfoMap.find(_user_broker_key);		
		if (userIt != g_userProcessInfoMap.end())
		{			
			UserProcessInfo_ptr userProcessInfoPtr = userIt->second;
			userProcessInfoPtr->user_connections_.erase(_connection_id);
			if (userProcessInfoPtr->ProcessIsRunning())
			{				
				userProcessInfoPtr->NotifyClose(_connection_id);
			}
		}		
		connection_manager_.stop(shared_from_this());		
	}
	catch (std::exception& ex)
	{
		Log().WithField("fun","OnCloseConnection")
			.WithField("key","gateway")
			.WithField("ip",_X_Real_IP)
			.WithField("agent",_agent)
			.WithField("analysis",_analysis)
			.WithField("connId",_connection_id)
			.WithField("fd",(int)m_ws_socket.next_layer().native_handle())
			.WithField("user_key",_user_broker_key)
			.Log(LOG_ERROR,"connection::OnCloseConnection()");		
	}	
}

void connection::OutputNotifySycn(long notify_code
	, const std::string& notify_msg, const char* level
	, const char* type)
{
	//构建数据包
	SerializerTradeBase nss;
	rapidjson::Pointer("/aid").Set(*nss.m_doc, "rtn_data");
	rapidjson::Value node_message;
	node_message.SetObject();
	node_message.AddMember("type", rapidjson::Value(type, nss.m_doc->GetAllocator()).Move(), nss.m_doc->GetAllocator());
	node_message.AddMember("level", rapidjson::Value(level, nss.m_doc->GetAllocator()).Move(), nss.m_doc->GetAllocator());
	node_message.AddMember("code", notify_code, nss.m_doc->GetAllocator());
	node_message.AddMember("content", rapidjson::Value(notify_msg.c_str(), nss.m_doc->GetAllocator()).Move(), nss.m_doc->GetAllocator());
	rapidjson::Pointer("/data/0/notify/N" + std::to_string(0)).Set(*nss.m_doc, node_message);
	std::string json_str;
	nss.ToString(&json_str);
	SendTextMsg(json_str);	
}