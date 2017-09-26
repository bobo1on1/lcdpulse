/*
 * lcdpulse
 * Copyright (C) Bob 2013
 * 
 * lcdpulse is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * lcdpulse is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lcdpulse.h"
#include <cstdio>
#include <sstream>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

using namespace std;

template <class Value>
inline std::string ToString(Value value)
{
  std::string data;
  std::stringstream valuestream;
  valuestream << value;
  valuestream >> data;
  return data;
}

inline int64_t GetTimeUs()
{
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return ((int64_t)time.tv_sec * 1000000LL) + (int64_t)(time.tv_nsec + 500) / 1000LL;
#else
  struct timeval time;
  gettimeofday(&time, NULL);
  return ((int64_t)time.tv_sec * 1000000LL) + (int64_t)time.tv_usec;
#endif
}

inline bool StrToInt(const std::string& data, int& value)
{
  return sscanf(data.c_str(), "%i", &value) == 1;
}

CLCDPulse::CLCDPulse(int argc, char *argv[])
{
  m_mainloop = NULL;
  m_context = NULL;
  m_state = PA_CONTEXT_UNCONNECTED;
  m_volume = 0;
  m_prevvolume = 0;
  m_sock = -1;
  m_port = 13666;
  m_lcdprocaddress = "localhost";

  bool hasaddress = false;
  bool daemonize = false;
  bool asoption = true;

  for (int i = 1; i < argc; i++)
  {
    if (asoption)
    {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
      {
        PrintHelpMessage();
        exit(1);
      }
      else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fork") == 0)
      {
        daemonize = true;
      }
      else if (strcmp(argv[i], "--") == 0)
      {
        asoption = false;
      }
      else if (!hasaddress)
      {
        ParseAddress(argv[i]);
        hasaddress = true;
      }
    }
    else if (!hasaddress)
    {
      ParseAddress(argv[i]);
      hasaddress = true;
    }
  }

  if (daemonize)
    Daemonize();
}

CLCDPulse::~CLCDPulse()
{
}

bool CLCDPulse::Setup()
{
  //set up pulseaudio as threaded mainloop
  m_mainloop = pa_threaded_mainloop_new();
  pa_threaded_mainloop_start(m_mainloop);

  pa_threaded_mainloop_lock(m_mainloop);

  pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
  m_context = pa_context_new(api, "lcdpulse");

  pa_context_set_state_callback(m_context, SStateCallback, this);
  pa_context_set_subscribe_callback(m_context, SSubscribeCallback, this);

  //try to connect to the pulseaudio daemon
  pa_context_connect(m_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);
  while(m_state != PA_CONTEXT_READY && m_state != PA_CONTEXT_FAILED && m_state != PA_CONTEXT_TERMINATED)
  {
    pa_threaded_mainloop_wait(m_mainloop);
    if (m_state == PA_CONTEXT_FAILED || m_state == PA_CONTEXT_TERMINATED)
    {
      printf("Pulse: pa_context_connect failed\n");
      return false;
    }
  }

  //subscribe to sink messages, to get a callback when the volume changes
  pa_subscription_mask_t mask = (pa_subscription_mask_t)((int)PA_SUBSCRIPTION_MASK_SINK | (int)PA_SUBSCRIPTION_MASK_SERVER);
  pa_operation* o = pa_context_subscribe(m_context, mask, SSuccessCallback, this);
  pa_operation_state_t state;
  while ((state = pa_operation_get_state(o)) != PA_OPERATION_DONE)
  {
    pa_threaded_mainloop_wait(m_mainloop);

    if (state == PA_OPERATION_CANCELLED)
    {
      printf("Pulse: pa_context_subscribe failed\n");
      return false;
    }
  }
  pa_operation_unref (o);

  o = pa_context_get_server_info(m_context, SServerInfoCallback, this);
  while ((state = pa_operation_get_state(o)) != PA_OPERATION_DONE)
  {
    pa_threaded_mainloop_wait(m_mainloop);

    if (state == PA_OPERATION_CANCELLED)
    {
      printf("Pulse: pa_context_get_server_info failed\n");
      return false;
    }
  }
  pa_operation_unref (o);

  o = pa_context_get_sink_info_by_name(m_context, m_defaultsink.c_str(), SSinkInfoCallback, this);
  pa_operation_unref(o);

  pa_threaded_mainloop_unlock(m_mainloop);

  //look up the host
  struct addrinfo *addrinfo = NULL;
  int rv = getaddrinfo(m_lcdprocaddress.c_str(), ToString(m_port).c_str(), NULL, &addrinfo);
  if (rv)
  {
    printf("LCDProc: Can't find host \"%s\":%i : %s\n", m_lcdprocaddress.c_str(), m_port, gai_strerror(rv));
    if (addrinfo)
      freeaddrinfo(addrinfo);
    return false;
  }

  //crease a socket and try to connect to LCDProc
  struct addrinfo *rp;
  for (rp = addrinfo; rp != NULL; rp = rp->ai_next)
  {
    m_sock = socket(rp->ai_family, SOCK_STREAM, IPPROTO_TCP);

    if (m_sock == -1) //can't make socket
      continue;

    if (connect(m_sock, rp->ai_addr, rp->ai_addrlen) != -1)
      break;

    close(m_sock);
  }

  freeaddrinfo(addrinfo);

  if (rp == NULL) // Check for connect() failures
  {
    printf("LCDProc: Can't connect to \"%s\":%i : %s\n", m_lcdprocaddress.c_str(), m_port, strerror(errno));
    return false;
  }
  else
  {
    printf("LCDProc: Connected to \"%s\":%i\n", m_lcdprocaddress.c_str(), m_port);
  }

  //set up a screen with a string widget on LCDProc
  try
  {
    WriteCommand("hello\n");
    WriteCommand("client_set -name lcdpulse\n");
    WriteCommand("screen_add volumescr\n");
    WriteCommand("screen_set volumescr -heartbeat off\n");
    WriteCommand("screen_set volumescr -priority background\n");
    WriteCommand("widget_add volumescr volumestr string\n");

    usleep(100000);
    PurgeSocket();
  }
  catch(...)
  {
    return false;
  }

  return true;
}

void CLCDPulse::Run()
{
  for(;;)
  {
    try
    {
      PurgeSocket();
    }
    catch(...)
    {
      return;
    }

    //wait until the pulseaudio volume changes
    pa_threaded_mainloop_lock(m_mainloop);
    while (m_state == PA_CONTEXT_READY && m_volume == m_prevvolume)
      pa_threaded_mainloop_wait(m_mainloop);

    //detected a change, store the volume locally and exit the lock
    m_prevvolume = m_volume;
    int volume = m_volume;
    pa_threaded_mainloop_unlock(m_mainloop);

    if (m_state != PA_CONTEXT_READY)
    {
      printf("Pulse: context failed\n");
      return;
    }

    //give the screen an alert status so that the volume shows always
    try
    {
      WriteCommand("screen_set volumescr -priority alert\n");
      WriteCommand(string("widget_set volumescr volumestr 1 1 \"Volume: ") + ToString(volume) + "%\"\n");
    }
    catch(...)
    {
      return;
    }

    //keep displaying the volume until 2 seconds after the last volume change
    int64_t end = GetTimeUs() + 2000000;
    while (GetTimeUs() < end && m_state == PA_CONTEXT_READY)
    {
      int64_t start = GetTimeUs();

      pa_threaded_mainloop_lock(m_mainloop);
      if (m_volume != m_prevvolume)
      {
        m_prevvolume = m_volume;
        volume = m_volume;
        pa_threaded_mainloop_unlock(m_mainloop);

        try
        {
          WriteCommand(string("widget_set volumescr volumestr 1 1 \"Volume: ") + ToString(m_volume) + "%\"\n");
        }
        catch(...)
        {
          return;
        }

        end = GetTimeUs() + 2000000;
      }
      else
      {
        pa_threaded_mainloop_unlock(m_mainloop);
      }

      int64_t end = GetTimeUs();
      int64_t loopremaining = 5000LL - (end - start);

      if (loopremaining > 0)
        usleep(loopremaining);
    }

    try
    {
      WriteCommand("screen_set volumescr -priority background\n");
    }
    catch(...)
    {
      return;
    }
  }
}

void CLCDPulse::Cleanup()
{
  if (m_sock != -1)
  {
    shutdown(m_sock, SHUT_RDWR);
    close(m_sock);
    m_sock = -1;
  }

  if (m_context && m_mainloop)
  {
    pa_threaded_mainloop_lock(m_mainloop);
    pa_context_disconnect(m_context);
    pa_context_unref(m_context);
    m_context = NULL;
    pa_threaded_mainloop_unlock(m_mainloop);
  }

  if (m_mainloop)
  {
    pa_threaded_mainloop_stop(m_mainloop);
    pa_threaded_mainloop_free(m_mainloop);
    m_mainloop= NULL;
  }

  m_state = PA_CONTEXT_UNCONNECTED;
}

void CLCDPulse::PrintHelpMessage()
{
  printf(
         "\n"
         "usage: lcdpulse [OPTION] [address:[port]]\n"
         "\n"
         "  examples for lcdproc address:\n"
         "\n"
         "  localhost:13666\n"
         "  somemachine.myplace.org\n"
         "  1111:2222:3333:4444:5555:6666:7777:8888\n"
         "  [aaaa:bbbb:cccc:dddd:eeee:ffff:1111:2222]:12345\n"
         "\n"
         "  If address is enclosed in brackets ([foo]), the string inside the backets\n"
         "  is used as the host, and if there is a colon (:) after the last bracket\n"
         "  the string after that is used as the port.\n"
         "  If address is not enclosed in brackets, and has one colon, the string after\n"
         "  the colon is used as the port.\n"
         "  Otherwise the entire string is used as the host.\n"
         "\n"
         "  options:\n"
         "\n"
         "    -f, --fork         daemonize\n"
         "    -h, --help         print this message\n"
         "    --                 don't interpret any next argument as option\n"
         "\n"
         );
}

void CLCDPulse::ParseAddress(std::string target)
{
  if (target.length() >= 2 && target[0] == '[') //ipv6 address with brackets
  {
    size_t endpos = target.find(']', 1);
    if (endpos != std::string::npos)
    {
      m_lcdprocaddress = target.substr(1, endpos - 1);
      if (endpos < target.length() - 3 && target[endpos + 1] == ':') //port
      {
        std::string portstr = target.substr(endpos + 2);
        if (!StrToInt(portstr, m_port))
        {
          printf("ERROR: Wrong value for port: \"%s\"\n", portstr.c_str());
          exit(1);
        }
      }
    }
  }
  else
  {
    size_t colonpos = target.find(':');
    if (colonpos != std::string::npos && colonpos < target.length() - 2 && colonpos > 0 && colonpos == target.find_last_of(':'))
    {
      //if there is only one colon and no brackets, assume ipv4 address or hostname with port
      m_lcdprocaddress = target.substr(0, colonpos);

      std::string portstr = target.substr(colonpos + 1);
      if (!StrToInt(portstr, m_port))
      {
        printf("ERROR: Wrong value for port: \"%s\"\n", portstr.c_str());
        exit(1);
      }
    }
    else //ipv4 or ipv6 address or hostname without port
    {
      m_lcdprocaddress = target;
    }
  }
}

void CLCDPulse::Daemonize()
{
  //fork a child process
  pid_t pid = fork();
  if (pid == -1)
    printf("ERROR: fork(): %s", strerror(errno));
  else if (pid > 0)
    exit(0); //this is the parent process, exit

  //detach the child process from the parent
  if (setsid() < 0)
    printf("ERROR: setsid(): %s", strerror(errno));

  //change the working directory to / so that the directory this is started from
  //is not locked by the daemon process
  if ((chdir("/")) < 0)
    printf("ERROR: chdir(): %s", strerror(errno));

  //redirect stdout and stderr to /dev/null
  freopen("/dev/null", "w", stdout);
  freopen("/dev/null", "w", stderr);
}

void CLCDPulse::SetNonBlock(bool nonblock)
{
  int flags = fcntl(m_sock, F_GETFL);
  if (flags == -1)
  {
    printf("LCDProc: F_GETFL: %s\n", strerror(errno));
    throw errno;
  }

  if (nonblock)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;
  
  if (fcntl(m_sock, F_SETFL, flags) == -1)
  {
    printf("LCDProc: F_SETFL: %s\n", strerror(errno));
    throw errno;
  }
}

void CLCDPulse::PurgeSocket()
{
  //read any data from the LCDProc socket, it's currently not used for anything

  SetNonBlock(true);

  char buf[1024];
  int returnv;
  while ((returnv = read(m_sock, buf, sizeof(buf))) > 0)
  {
    printf("LCDProc read: ");
    for (int i = 0; i < returnv; i++)
      putc(buf[i], stdout);
  }

  if (returnv < 0 && (errno != EAGAIN && errno != EINTR))
  {
    printf("LCDProc: error reading socket: %s\n", strerror(errno));
    throw errno;
  }

  SetNonBlock(false);
}

void CLCDPulse::WriteCommand(const std::string& cmd)
{
  PurgeSocket();

  size_t written = 0;
  ssize_t returnv;
  while (written < cmd.length())
  {
    returnv = write(m_sock, cmd.c_str() + written, cmd.length() - written);

    if (returnv > 0)
    {
      written += returnv;
    }
    else if (errno != EINTR)
    {
      printf("LCDProc: Error writing socket: %s\n", strerror(errno));
      throw errno;
    }
  }

  PurgeSocket();
}

void CLCDPulse::SStateCallback(pa_context* c, void *userdata)
{
  ((CLCDPulse*)userdata)->StateCallback(c);
}

void CLCDPulse::StateCallback(pa_context* c)
{
  m_state = pa_context_get_state(c);

  if (m_state == PA_CONTEXT_UNCONNECTED)
    printf("Pulse: Unconnected\n");
  else if (m_state == PA_CONTEXT_CONNECTING)
    printf("Pulse: Connecting\n");
  else if (m_state == PA_CONTEXT_AUTHORIZING)
    printf("Pulse: Authorizing\n");
  else if (m_state == PA_CONTEXT_SETTING_NAME)
    printf("Pulse: Setting name\n");
  else if (m_state == PA_CONTEXT_READY)
    printf("Pulse: Ready\n");
  else if (m_state == PA_CONTEXT_FAILED)
    printf("Pulse: Connect failed\n");
  else if (m_state == PA_CONTEXT_TERMINATED)
    printf("Pulse: Connection terminated\n");

  pa_threaded_mainloop_signal(m_mainloop, 0);
}

void CLCDPulse::SSubscribeCallback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  ((CLCDPulse*)userdata)->SubscribeCallback(c, t, idx);
}

void CLCDPulse::SubscribeCallback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx)
{
  if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE)
    return;

  //default sink might have changed, check it
  if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER)
  {
    pa_operation* o = pa_context_get_server_info(m_context, SServerInfoCallback, this);
    pa_operation_unref(o);
  }

  //get the current volume
  pa_operation* o = pa_context_get_sink_info_by_name(m_context, m_defaultsink.c_str(), SSinkInfoCallback, this);
  pa_operation_unref(o);
}

void CLCDPulse::SSuccessCallback(pa_context *c, int success, void *userdata)
{
  ((CLCDPulse*)userdata)->SuccessCallback(c, success);
}

void CLCDPulse::SuccessCallback(pa_context *c, int success)
{
  pa_threaded_mainloop_signal(m_mainloop, 0);
}

void CLCDPulse::SSinkInfoCallback(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
  ((CLCDPulse*)userdata)->SinkInfoCallback(c, i, eol);
}

void CLCDPulse::SinkInfoCallback(pa_context *c, const pa_sink_info *i, int eol)
{
  if (eol)
    return;

  int volume;
  if (i->mute)
  {
    volume = 0;
  }
  else
  {
    pa_volume_t avg = pa_cvolume_avg(&i->volume);
    volume = lround((double)avg / PA_VOLUME_NORM * 100.0);
  }

  if (volume != m_volume)
  {
    printf("Pulse: volume: %i%%\n", volume);
    m_volume = volume;
    pa_threaded_mainloop_signal(m_mainloop, 0);
  }
}

void CLCDPulse::SServerInfoCallback(pa_context *c, const pa_server_info*i, void *userdata)
{
  ((CLCDPulse*)userdata)->ServerInfoCallback(c, i);
}

void CLCDPulse::ServerInfoCallback(pa_context *c, const pa_server_info*i)
{
  printf("default sink is \"%s\"\n", i->default_sink_name);
  m_defaultsink = i->default_sink_name;
  pa_threaded_mainloop_signal(m_mainloop, 0);
}

