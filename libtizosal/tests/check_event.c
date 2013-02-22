/**
 * Copyright (C) 2011-2013 Aratelia Limited - Juan A. Rubio
 *
 * This file is part of Tizonia
 *
 * Tizonia is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Tizonia is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Tizonia.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file   check_event.c
 * @author Juan A. Rubio <juan.rubio@aratelia.com>
 *
 * @brief  Event loop API unit tests
 *
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>

#define CHECK_EVENT_SERV_PORT 9877
#define MAXLINE 4096
#define CHECK_MSG "Hello there!"
#define CHECK_ECHO_CMD "/bin/bash -c \"echo -n \"Hello\\ there!\" > /dev/udp/127.0.0.1/9877\""
#define CHECK_TIMER_PERIOD 1.5

static bool g_io_cback_received = false;
static int g_timeout_count = 5;
static int g_restart_count = 2;
static bool g_timer_restarted = false;

static void
check_event_io_cback (tiz_event_io_t * ap_ev_io, int fd, int events)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;
  int rcvfromrc;
  char msg [MAXLINE];
  struct sockaddr_in cliaddr;
  socklen_t len = sizeof (cliaddr);

  TIZ_LOG (TIZ_LOG_TRACE, "io cback received");

  fail_if (NULL == ap_ev_io);
  fail_if ((events & TIZ_EVENT_READ) == 0);

  error = tiz_event_io_stop (ap_ev_io);
  fail_if (OMX_ErrorNone != error);

  rcvfromrc = recvfrom (fd, msg, MAXLINE, 0, (struct sockaddr *) &cliaddr,
                        &len);
  fail_if (rcvfromrc < 0);

  msg [rcvfromrc] = '\000';
  TIZ_LOG (TIZ_LOG_TRACE, "received : [%s]", msg);

  fail_if (strncmp(msg, CHECK_MSG, strlen (msg) != 0));

  g_io_cback_received = true;
}

static int
start_udp_server()
{
  int sockfd, bindrc, fcntlrc, flags;
  struct sockaddr_in servaddr;

  sockfd = socket (AF_INET, SOCK_DGRAM, 0);
  fail_if (sockfd < 0);

  bzero (&servaddr, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl (INADDR_ANY);
  servaddr.sin_port        = htons (CHECK_EVENT_SERV_PORT);

  bindrc = bind (sockfd, (const struct sockaddr *) &servaddr,
                 sizeof(servaddr));
  fail_if (bindrc < 0);

  flags = fcntl(sockfd, F_GETFL, 0);
  fail_if (flags < 0);

  flags |= O_NONBLOCK;
  fcntlrc = fcntl(sockfd, F_SETFL, flags);
  fail_if (fcntlrc < 0);

  return sockfd;
}

static void
stop_udp_server(int sockfd)
{
  close(sockfd);
}

static void
check_event_timer_cback (tiz_event_timer_t * ap_ev_timer, int events)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;

  TIZ_LOG (TIZ_LOG_TRACE, "timer cback received - timeout count [%d]",
           g_timeout_count);

  fail_if (NULL == ap_ev_timer);

  if (--g_timeout_count == 0)
    {
      if (false == g_timer_restarted)
        {
          TIZ_LOG (TIZ_LOG_TRACE, "timeout count [%d] - restarting timer",
                   g_timeout_count);
          error = tiz_event_timer_restart (ap_ev_timer);
          fail_if (OMX_ErrorNone != error);
          g_timeout_count = g_restart_count;
          g_timer_restarted = true;
        }
      else
        {
          TIZ_LOG (TIZ_LOG_TRACE, "timeout count [%d] - stopping timer",
                   g_timeout_count);
          error = tiz_event_timer_stop (ap_ev_timer);
          fail_if (OMX_ErrorNone != error);
        }
    }
}


/* TESTS */

START_TEST (test_event_loop_init_and_destroy)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;

  error = tiz_event_loop_init ();
  fail_if (error != OMX_ErrorNone);

  error = tiz_event_loop_init ();
  fail_if (error != OMX_ErrorNone);

/*   tiz_event_loop_destroy (); */
  tiz_event_loop_destroy ();
}
END_TEST

START_TEST (test_event_io)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;
  tiz_event_io_t * p_ev_io = NULL;
  int fd = 0;
  char cmd [128];

  error = tiz_event_loop_init ();
  fail_if (error != OMX_ErrorNone);

  fd = start_udp_server();

  error = tiz_event_io_init (&p_ev_io, check_event_io_cback,
                             fd, TIZ_EVENT_READ);
  fail_if (error != OMX_ErrorNone);

  TIZ_LOG (TIZ_LOG_TRACE, "started io watcher");

  error = tiz_event_io_start (p_ev_io);
  fail_if (error != OMX_ErrorNone);

  snprintf(cmd, strlen (CHECK_ECHO_CMD) + 1, "%s", CHECK_ECHO_CMD);
  TIZ_LOG (TIZ_LOG_TRACE, "cmd = [%s]", cmd);

  fail_if (-1 == system (cmd));

  fail_if (true != g_io_cback_received);

  error = tiz_event_io_stop (p_ev_io);
  fail_if (error != OMX_ErrorNone);

  tiz_event_io_destroy (p_ev_io);

  stop_udp_server(fd);

  tiz_event_loop_destroy ();
}
END_TEST

START_TEST (test_event_timer)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;
  tiz_event_timer_t * p_ev_timer = NULL;
  int sleep_len = (double)(CHECK_TIMER_PERIOD * g_timeout_count) +
    (double)(CHECK_TIMER_PERIOD * g_restart_count) + 1;

  error = tiz_event_loop_init ();
  fail_if (error != OMX_ErrorNone);

  error = tiz_event_timer_init (&p_ev_timer, check_event_timer_cback,
                             1., CHECK_TIMER_PERIOD);
  fail_if (error != OMX_ErrorNone);

  error = tiz_event_timer_start (p_ev_timer);
  fail_if (error != OMX_ErrorNone);

  TIZ_LOG (TIZ_LOG_TRACE, "started timer watcher - sleep_len [%d]", sleep_len);

  sleep (sleep_len);

  fail_if (0 != g_timeout_count);

  tiz_event_timer_destroy (p_ev_timer);

  tiz_event_loop_destroy ();
  tiz_event_loop_destroy ();
}
END_TEST
