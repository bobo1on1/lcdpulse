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

#ifndef LCDPULSE_H
#define LCDPULSE_H

#include <pulse/pulseaudio.h>
#include <string>
#include "../build/config.h"

class CLCDPulse
{
  public:
    CLCDPulse(int argc, char *argv[]);
    ~CLCDPulse();

    bool Setup();
    void Run();
    void Cleanup();

  private:
    pa_threaded_mainloop* m_mainloop;
    pa_context*           m_context;
    pa_context_state_t    m_state;
    std::string           m_defaultsink;
    int                   m_volume;
    int                   m_prevvolume;

    std::string           m_lcdprocaddress;
    int                   m_sock;
    int                   m_port;

    void        PrintHelpMessage();
    void        ParseAddress(std::string target);
    void        Daemonize();
    void        SetNonBlock(bool nonblock);
    void        PurgeSocket();
    bool        WaitSuccess();
    void        WriteCommand(const std::string& cmd, bool waitsuccess = true);

    static void SStateCallback(pa_context* c, void *userdata);
    void        StateCallback(pa_context* c);

    static void SSubscribeCallback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata);
    void        SubscribeCallback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx);

    static void SSuccessCallback(pa_context *c, int success, void *userdata);
    void        SuccessCallback(pa_context *c, int success);

    static void SSinkInfoCallback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
    void        SinkInfoCallback(pa_context *c, const pa_sink_info *i, int eol);

    static void SServerInfoCallback(pa_context *c, const pa_server_info*i, void *userdata);
    void        ServerInfoCallback(pa_context *c, const pa_server_info*i);
};

#endif //LCDPULSE_H
