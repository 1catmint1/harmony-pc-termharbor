// SPDX-License-Identifier: BSD-3-Clause
// Loopback-only interactive workspace. No OS shell or host files are exposed.
#include "local_experience.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/sftp.h>
#include <hilog/log.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr size_t kMaxFile = 1024 * 1024;
constexpr size_t kMaxFiles = 16;
using Clock = std::chrono::steady_clock;
struct FileHandle { std::string name; bool directory=false; bool listed=false; bool writable=false; };

class Experience {
 public:
  std::atomic<bool> running{false};
  std::atomic<unsigned> tcpEchoes{0}, sshLogins{0}, fileReads{0}, fileWrites{0};
  std::atomic<unsigned> scpTransfers{0},sftpRequests{0};
  int tcpPort=0, sshPort=0;
  std::string error() { std::lock_guard<std::mutex> lock(errorMutex);return lastError; }
  ~Experience() { stop(); }

  void start() {
    if (running && live()) return;
    stop();
    note("");
    files.clear();
    files["hello.txt"] = "TermHarbor 本机连接体验\n真实 TCP / SSH / SCP / SFTP。\n本文件仅位于体验服务内存中。\n";
    tcpEchoes=0; sshLogins=0; fileReads=0; fileWrites=0;scpTransfers=0;sftpRequests=0;
    try {
      if (ssh_init()!=SSH_OK || ssh_pki_generate(SSH_KEYTYPE_ECDSA_P256,256,&key)!=SSH_OK)
        throw std::runtime_error("Cannot initialize local SSH host key");
      bind=ssh_bind_new();
      if (!bind) throw std::runtime_error("Cannot allocate local SSH endpoint");
      int zero=0;
      if (ssh_bind_options_set(bind,SSH_BIND_OPTIONS_PROCESS_CONFIG,&zero)!=SSH_OK ||
          ssh_bind_options_set(bind,SSH_BIND_OPTIONS_BINDADDR,"127.0.0.1")!=SSH_OK ||
          ssh_bind_options_set(bind,SSH_BIND_OPTIONS_BINDPORT,&zero)!=SSH_OK ||
          ssh_bind_options_set(bind,SSH_BIND_OPTIONS_IMPORT_KEY,key)!=SSH_OK)
        throw std::runtime_error("Cannot listen on local SSH endpoint");
      // IMPORT_KEY transfers ownership to ssh_bind; it is released by
      // ssh_bind_free even when the subsequent listen operation fails.
      key=nullptr;
      if(ssh_bind_listen(bind)!=SSH_OK)throw std::runtime_error("Cannot listen on local SSH endpoint");
      sshPort=portOf(ssh_bind_get_fd(bind));
      tcp=socket(AF_INET,SOCK_STREAM,0);
      sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
      if (tcp<0 || ::bind(tcp,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0 || listen(tcp,4)!=0)
        throw std::runtime_error("Cannot listen on local TCP endpoint");
      tcpPort=portOf(tcp);
      deadline=Clock::now()+std::chrono::minutes(20);
      running=true;
      tcpThread=std::thread([this]{ acceptTcp(); });
      sshThread=std::thread([this]{ acceptSsh(); });
    } catch (...) { stop();throw; }
  }

  void stop() {
    running=false;
    {std::lock_guard<std::mutex> lock(socketMutex);for(int fd:sockets)shutdown(fd,SHUT_RDWR);}
    if (tcp>=0) shutdown(tcp,SHUT_RDWR);
    if (bind) shutdown(ssh_bind_get_fd(bind),SHUT_RDWR);
    if (tcpThread.joinable())tcpThread.join();
    if (sshThread.joinable())sshThread.join();
    for(auto& worker:workers)if(worker.joinable())worker.join();
    workers.clear();sockets.clear();
    {std::lock_guard<std::mutex> lock(fileMutex);files.clear();}
    if(tcp>=0){close(tcp);tcp=-1;}
    if(bind){ssh_bind_free(bind);bind=nullptr;}
    if(key){ssh_key_free(key);key=nullptr;}
    tcpPort=0;sshPort=0;
  }

 private:
  int tcp=-1;
  ssh_bind bind=nullptr;
  ssh_key key=nullptr;
  Clock::time_point deadline;
  std::thread tcpThread,sshThread;
  std::vector<std::thread> workers;
  std::set<int> sockets;
  std::mutex socketMutex,workerMutex,fileMutex;
  std::mutex errorMutex;
  std::string lastError;
  void note(const std::string& message) { std::lock_guard<std::mutex> lock(errorMutex);lastError=message; }
  std::map<std::string,std::string> files;

  static int portOf(int fd){sockaddr_in a{};socklen_t n=sizeof(a);if(getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)!=0)throw std::runtime_error("Cannot read local port");return ntohs(a.sin_port);}
  bool live(){return running && Clock::now()<deadline;}
  bool track(int fd){std::lock_guard<std::mutex> l(socketMutex);if(sockets.size()>=8)return false;sockets.insert(fd);return true;}
  void untrack(int fd){std::lock_guard<std::mutex> l(socketMutex);sockets.erase(fd);}
  bool ready(int fd){pollfd p{fd,POLLIN,0};return poll(&p,1,200)>0 && (p.revents&POLLIN);}
  bool addWorker(std::thread worker){std::lock_guard<std::mutex> l(workerMutex);workers.push_back(std::move(worker));return true;}
  bool capacity(){std::lock_guard<std::mutex> l(workerMutex);return workers.size()<64;}

  void acceptTcp(){
    while(live()){
      if(!ready(tcp))continue;
      int fd=accept(tcp,nullptr,nullptr);if(fd<0)continue;
      if(!capacity()||!track(fd)){close(fd);continue;}
      addWorker(std::thread([this,fd]{
        const std::string banner="TermHarbor 本机 TCP 回声服务\r\n输入文字并发送，即可查看真实网络往返。\r\n";
        send(fd,banner.data(),banner.size(),MSG_NOSIGNAL);
        char buf[4096];
        while(live()){
          if(!ready(fd))continue;
          ssize_t n=recv(fd,buf,sizeof(buf),0);if(n<=0)break;
          size_t done=0;while(done<static_cast<size_t>(n)){ssize_t w=send(fd,buf+done,n-done,MSG_NOSIGNAL);if(w<=0)break;done+=w;}
          if(done!=static_cast<size_t>(n))break;
          ++tcpEchoes;
        }
        untrack(fd);close(fd);
      }));
    }
  }

  void acceptSsh(){
    while(live()){
      if(!ready(ssh_bind_get_fd(bind)))continue;
      ssh_session session=ssh_new();if(!session)continue;
      long timeout=5;ssh_options_set(session,SSH_OPTIONS_TIMEOUT,&timeout);
      if(ssh_bind_accept(bind,session)!=SSH_OK){note(std::string("SSH accept: ")+ssh_get_error(bind));ssh_free(session);continue;}
      int fd=ssh_get_fd(session);
      if(!capacity()||!track(fd)){ssh_disconnect(session);ssh_free(session);continue;}
      addWorker(std::thread([this,session,fd]{
        handleSsh(session);
        untrack(fd);ssh_disconnect(session);ssh_free(session);
      }));
    }
  }

  bool write(ssh_channel c,const std::string& s){size_t pos=0;while(live()&&pos<s.size()){int n=ssh_channel_write(c,s.data()+pos,s.size()-pos);if(n==SSH_AGAIN){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}if(n<=0){note(std::string("SSH write: ")+ssh_get_error(ssh_channel_get_session(c)));return false;}pos+=n;}return pos==s.size();}
  bool readExact(ssh_channel c,std::string& out,size_t length){out.clear();char b[4096];while(live()&&out.size()<length){int n=ssh_channel_read_timeout(c,b,std::min(sizeof(b),length-out.size()),0,500);if(n==SSH_AGAIN)continue;if(n<0)return false;if(n==0){if(ssh_channel_is_eof(c))return false;continue;}out.append(b,n);}return out.size()==length;}
  bool readLine(ssh_channel c,std::string& out){out.clear();while(live()&&out.size()<512){std::string one;if(!readExact(c,one,1))return false;if(one[0]=='\n')return true;out+=one;}return false;}
  static std::string fileName(std::string name){
    while(!name.empty()&&(name.front()=='/'||name.front()=='\''||name.front()=='"'))name.erase(0,1);
    while(!name.empty()&&(name.back()=='\''||name.back()=='"'))name.pop_back();
    if(name.size()>64||name.empty()||name.find("..")!=std::string::npos||name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos)return "";
    for(unsigned char c:name)if(c<32)return "";
    return name;
  }
  std::string command(const std::string& text){
    if(text=="help"||text.empty())return "help | echo 文字 | pwd | ls | cat /hello.txt | exit\r\n";
    if(text=="pwd")return "/\r\n";
    if(text=="ls"){std::lock_guard<std::mutex> l(fileMutex);std::string r;for(const auto& f:files)r+=f.first+"\r\n";return r;}
    if(text.rfind("echo ",0)==0)return text.substr(5)+"\r\n";
    if(text.rfind("cat ",0)==0){auto name=fileName(text.substr(4));std::lock_guard<std::mutex> l(fileMutex);auto i=files.find(name);return i==files.end()?"No such demo file\r\n":i->second;}
    return "Local workspace command not supported; type help.\r\n";
  }
  void shell(ssh_channel c){
    if(!write(c,"TermHarbor 本机 SSH 体验（仅内存工作区）\r\n"+command("help")+"local-demo> "))return;
    std::string line;char b[1024];
    while(live()&&ssh_channel_is_open(c)&&!ssh_channel_is_eof(c)){
      int n=ssh_channel_read_timeout(c,b,sizeof(b),0,200);if(n==SSH_AGAIN)continue;if(n<0){note(std::string("SSH read: ")+ssh_get_error(ssh_channel_get_session(c)));break;}if(n==0)continue;
      for(int i=0;i<n;++i){unsigned char ch=b[i];
        if(ch=='\r'||ch=='\n'){
          if(line=="exit"){write(c,"\r\nBye\r\n");return;}
          if(!write(c,"\r\n"+command(line)+"local-demo> "))return;line.clear();
        }else if(ch==127||ch==8){if(!line.empty()){line.pop_back();write(c,"\b \b");}}
        else if(ch>=32&&line.size()<4096){line+=static_cast<char>(ch);if(!write(c,std::string(1,static_cast<char>(ch))))return;}
      }
    }
  }
  void scp(ssh_channel c,const std::string& text){
    auto end=text.find(' ',4);if(end==std::string::npos)return;
    std::string flags=text.substr(4,end-4);
    bool upload=flags=="-t"||flags=="-pt";
    bool preserve=flags=="-pf"||flags=="-pt";
    if(!upload&&flags!="-f"&&flags!="-pf")return;
    auto name=fileName(text.substr(end+1));if(name.empty()){write(c,"\1Invalid local workspace path\n");return;}
    std::string ack,data;
    if(upload){
      if(!write(c,std::string(1,'\0'))||!readLine(c,data))return;
      if(preserve&&data.rfind("T",0)==0){if(!write(c,std::string(1,'\0'))||!readLine(c,data))return;}
      std::istringstream header(data);std::string mode,filename;size_t len=0;header>>mode>>len>>filename;
      if(mode!="C0644"&&mode!="C0600"){write(c,"\1Invalid file mode\n");return;}
      if(len>kMaxFile){write(c,"\1Demo file limit is 1 MiB\n");return;}
      {std::lock_guard<std::mutex> l(fileMutex);if(files.size()>=kMaxFiles&&!files.count(name)){write(c,"\1Demo workspace is full\n");return;}}
      if(!write(c,std::string(1,'\0'))||!readExact(c,data,len)||!readExact(c,ack,1)||ack[0]!=0)return;
      {std::lock_guard<std::mutex> l(fileMutex);files[name]=data;}
      ++fileWrites;if(write(c,std::string(1,'\0')))++scpTransfers;
    }else{
      {std::lock_guard<std::mutex> l(fileMutex);auto i=files.find(name);if(i==files.end()){write(c,"\1No such demo file\n");return;}data=i->second;}
      if(!readExact(c,ack,1)||ack[0]!=0)return;
      if(preserve&&(!write(c,"T0 0 0 0\n")||!readExact(c,ack,1)||ack[0]!=0))return;
      if(!write(c,"C0644 "+std::to_string(data.size())+" "+name+"\n")||!readExact(c,ack,1)||ack[0]!=0)return;
      if(write(c,data)&&write(c,std::string(1,'\0'))&&readExact(c,ack,1)&&ack[0]==0){++fileReads;++scpTransfers;}
    }
  }

  void sftp(ssh_session session,ssh_channel channel){
    sftp_session server=sftp_server_new(session,channel);if(!server)return;
    if(sftp_server_init(server)!=SSH_OK){sftp_server_free(server);return;}
    std::vector<FileHandle*> handles;
    while(live()){
      sftp_client_message m=sftp_get_client_message(server);if(!m)break;
      uint8_t type=sftp_client_message_get_type(m);const char* raw=sftp_client_message_get_filename(m);
      std::string name=fileName(raw?raw:"");
      auto handle=(m->handle ? static_cast<FileHandle*>(sftp_handle(server,m->handle)) : nullptr);
      auto status=[&](uint32_t code){sftp_reply_status(m,code,code==SSH_FX_OK?"OK":"Local workspace request rejected");};
      if(type==SSH_FXP_REALPATH){sftp_attributes_struct a{};a.flags=SSH_FILEXFER_ATTR_PERMISSIONS;a.permissions=S_IFDIR|0700;sftp_reply_name(m,"/",&a);}
      else if(type==SSH_FXP_OPEN||type==SSH_FXP_OPENDIR){
        bool directory=type==SSH_FXP_OPENDIR;bool writable=(m->flags&SSH_FXF_WRITE)!=0;
        std::lock_guard<std::mutex> l(fileMutex);
        if(!directory&&(name.empty()||(!files.count(name)&&(!(m->flags&SSH_FXF_CREAT)||files.size()>=kMaxFiles))))status(SSH_FX_NO_SUCH_FILE);
        else if(handles.size()>=32)status(SSH_FX_FAILURE);
        else{if(!directory){if(!files.count(name)||((m->flags&SSH_FXF_TRUNC)&&writable))files[name]="";}
          auto h=new FileHandle{name,directory,false,writable};ssh_string id=sftp_handle_alloc(server,h);
          if(id){handles.push_back(h);sftp_reply_handle(m,id);ssh_string_free(id);}else{delete h;status(SSH_FX_FAILURE);}}
      }else if(type==SSH_FXP_CLOSE){if(!handle)status(SSH_FX_INVALID_HANDLE);else{sftp_handle_remove(server,handle);handles.erase(std::remove(handles.begin(),handles.end(),handle),handles.end());delete handle;status(SSH_FX_OK);}}
      else if(type==SSH_FXP_STAT||type==SSH_FXP_LSTAT||type==SSH_FXP_FSTAT){
        if(type==SSH_FXP_FSTAT&&handle)name=handle->name;
        std::lock_guard<std::mutex> l(fileMutex);auto i=files.find(name);
        if(i==files.end())status(SSH_FX_NO_SUCH_FILE);else{sftp_attributes_struct a{};a.flags=SSH_FILEXFER_ATTR_SIZE|SSH_FILEXFER_ATTR_PERMISSIONS;a.size=i->second.size();a.permissions=S_IFREG|0600;sftp_reply_attr(m,&a);}
      }else if(type==SSH_FXP_READ){
        if(!handle||handle->directory)status(SSH_FX_INVALID_HANDLE);
        else{std::lock_guard<std::mutex> l(fileMutex);auto& data=files[handle->name];if(m->offset>=data.size())status(SSH_FX_EOF);else{size_t length=std::min<size_t>({m->len,data.size()-m->offset,65536});sftp_reply_data(m,data.data()+m->offset,length);++fileReads;++sftpRequests;}}
      }else if(type==SSH_FXP_WRITE){
        size_t len=m->data?ssh_string_len(m->data):0;
        if(!handle||handle->directory||!handle->writable)status(SSH_FX_PERMISSION_DENIED);
        else if(m->offset>kMaxFile||len>kMaxFile-m->offset)status(SSH_FX_FAILURE);
        else{std::lock_guard<std::mutex> l(fileMutex);auto& data=files[handle->name];if(data.size()<m->offset+len)data.resize(m->offset+len);if(len)std::memcpy(data.data()+m->offset,ssh_string_data(m->data),len);++fileWrites;++sftpRequests;status(SSH_FX_OK);}
      }else if(type==SSH_FXP_READDIR){
        if(!handle||!handle->directory)status(SSH_FX_INVALID_HANDLE);else if(handle->listed)status(SSH_FX_EOF);
        else{std::lock_guard<std::mutex> l(fileMutex);for(const auto& f:files){sftp_attributes_struct a{};a.flags=SSH_FILEXFER_ATTR_SIZE|SSH_FILEXFER_ATTR_PERMISSIONS;a.size=f.second.size();a.permissions=S_IFREG|0600;sftp_reply_names_add(m,f.first.c_str(),f.first.c_str(),&a);}sftp_reply_names(m);handle->listed=true;}
      }else status(SSH_FX_OP_UNSUPPORTED);
      sftp_client_message_free(m);
    }
    for(auto* h:handles){sftp_handle_remove(server,h);delete h;}
    sftp_server_free(server);
  }

  void forward(ssh_channel channel) {
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(tcpPort);
    if(!track(fd)){close(fd);return;}
    if(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0){
      char buffer[4096];
      while(live()&&!ssh_channel_is_eof(channel)){
        int n=ssh_channel_read_timeout(channel,buffer,sizeof(buffer),0,30);
        if(n<0&&n!=SSH_AGAIN)break;
        if(n>0){size_t done=0;while(done<static_cast<size_t>(n)){ssize_t count=send(fd,buffer+done,n-done,MSG_NOSIGNAL);if(count<=0)break;done+=count;}if(done!=static_cast<size_t>(n))break;}
        pollfd p{fd,POLLIN,0};
        if(poll(&p,1,30)>0){ssize_t count=recv(fd,buffer,sizeof(buffer),0);if(count<=0||!write(channel,std::string(buffer,count)))break;}
      }
    }
    untrack(fd);close(fd);
  }

  void handleSsh(ssh_session session){
    if(ssh_handle_key_exchange(session)!=SSH_OK){note(std::string("SSH key exchange: ")+ssh_get_error(session));OH_LOG_Print(LOG_APP,LOG_ERROR,0x1234,"TermHarbor","Local SSH key exchange: %{public}s",ssh_get_error(session));return;}
    bool authenticated=false;unsigned tries=0;ssh_channel channel=nullptr;
    while(live()&&tries<8){
      ssh_message msg=ssh_message_get(session);
      if(!msg){
        // An authenticated forwarding session may wait for its first client.
        // A message wait timeout does not mean that the SSH peer disconnected.
        if(live()&&ssh_is_connected(session)&&ssh_get_error_code(session)==SSH_NO_ERROR)continue;
        note(std::string("SSH request: ")+ssh_get_error(session));
        OH_LOG_Print(LOG_APP,LOG_ERROR,0x1234,"TermHarbor","Local SSH request: %{public}s",ssh_get_error(session));break;
      }
      int type=ssh_message_type(msg),subtype=ssh_message_subtype(msg);
      bool handled=false,serveShell=false,serveSftp=false,serveForward=false;std::string exec;
      if(type==SSH_REQUEST_AUTH){
        ++tries;
        const char* user=ssh_message_auth_user(msg);const char* password=ssh_message_auth_password(msg);
        if(subtype==SSH_AUTH_METHOD_PASSWORD&&user&&password&&std::strcmp(user,"local-demo")==0&&std::strcmp(password,"local-demo")==0){
          ssh_message_auth_reply_success(msg,0);authenticated=true;++sshLogins;handled=true;
        }else ssh_message_auth_set_methods(msg,SSH_AUTH_METHOD_PASSWORD);
      }else if(authenticated&&type==SSH_REQUEST_CHANNEL_OPEN&&subtype==SSH_CHANNEL_SESSION&&!channel){channel=ssh_message_channel_request_open_reply_accept(msg);handled=channel!=nullptr;}
      else if(authenticated&&type==SSH_REQUEST_CHANNEL_OPEN&&subtype==SSH_CHANNEL_DIRECT_TCPIP&&!channel){
        const char* host=ssh_message_channel_request_open_destination(msg);
        if(host&&std::strcmp(host,"127.0.0.1")==0&&ssh_message_channel_request_open_destination_port(msg)==tcpPort){channel=ssh_message_channel_request_open_reply_accept(msg);handled=channel!=nullptr;serveForward=handled;}
      }
      else if(authenticated&&channel&&type==SSH_REQUEST_CHANNEL){
        if(subtype==SSH_CHANNEL_REQUEST_PTY||subtype==SSH_CHANNEL_REQUEST_ENV||subtype==SSH_CHANNEL_REQUEST_WINDOW_CHANGE){ssh_message_channel_request_reply_success(msg);handled=true;}
        else if(subtype==SSH_CHANNEL_REQUEST_SHELL){ssh_message_channel_request_reply_success(msg);handled=true;serveShell=true;}
        else if(subtype==SSH_CHANNEL_REQUEST_EXEC){const char* cmd=ssh_message_channel_request_command(msg);exec=cmd?cmd:"";ssh_message_channel_request_reply_success(msg);handled=true;}
        else if(subtype==SSH_CHANNEL_REQUEST_SUBSYSTEM){const char* sub=ssh_message_channel_request_subsystem(msg);if(sub&&std::strcmp(sub,"sftp")==0){ssh_message_channel_request_reply_success(msg);handled=true;serveSftp=true;}}
      }
      if(!handled)ssh_message_reply_default(msg);
      ssh_message_free(msg);
      if(serveShell){shell(channel);break;}
      if(serveSftp){sftp(session,channel);break;}
      if(serveForward){forward(channel);break;}
      if(!exec.empty()){if(exec.rfind("scp -",0)==0)scp(channel,exec);else write(channel,command(exec));break;}
    }
    if(channel){ssh_channel_request_send_exit_status(channel,0);ssh_channel_send_eof(channel);ssh_channel_close(channel);ssh_channel_free(channel);}
  }
};

Experience experience;
void text(napi_env env,napi_value object,const char* name,const std::string& value){napi_value v;napi_create_string_utf8(env,value.c_str(),value.size(),&v);napi_set_named_property(env,object,name,v);}
void number(napi_env env,napi_value object,const char* name,unsigned value){napi_value v;napi_create_uint32(env,value,&v);napi_set_named_property(env,object,name,v);}
napi_value info(napi_env env){napi_value o;napi_create_object(env,&o);text(env,o,"error",experience.error());text(env,o,"host","127.0.0.1");text(env,o,"username","local-demo");text(env,o,"password","local-demo");text(env,o,"remotePath","/hello.txt");number(env,o,"scpTransfers",experience.scpTransfers);number(env,o,"sftpRequests",experience.sftpRequests);number(env,o,"tcpPort",experience.tcpPort);number(env,o,"sshPort",experience.sshPort);number(env,o,"tcpEchoes",experience.tcpEchoes);number(env,o,"sshLogins",experience.sshLogins);number(env,o,"fileReads",experience.fileReads);number(env,o,"fileWrites",experience.fileWrites);return o;}
napi_value start(napi_env env,napi_callback_info){try{experience.start();return info(env);}catch(const std::exception& e){napi_throw_error(env,nullptr,e.what());return nullptr;}}
napi_value stop(napi_env env,napi_callback_info){experience.stop();return info(env);}
napi_value status(napi_env env,napi_callback_info){return info(env);}
}

void RegisterLocalExperience(napi_env env,napi_value exports){
  napi_property_descriptor descriptors[]={
    {"startLocalExperience",nullptr,start,nullptr,nullptr,nullptr,napi_default,nullptr},
    {"stopLocalExperience",nullptr,stop,nullptr,nullptr,nullptr,napi_default,nullptr},
    {"localExperienceStatus",nullptr,status,nullptr,nullptr,nullptr,napi_default,nullptr}
  };
  napi_define_properties(env,exports,3,descriptors);
  napi_add_env_cleanup_hook(env,[](void*){experience.stop();},nullptr);
}
