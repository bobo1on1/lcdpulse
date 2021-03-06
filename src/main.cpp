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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
  CLCDPulse lcdpulse(argc, argv);

  for(;;)
  {
    if (!lcdpulse.Setup())
    {
      printf("Setup failed, retrying in 10 seconds\n");
      lcdpulse.Cleanup();
      sleep(10);
      continue;
    }

    lcdpulse.Run();

    sleep(1); //prevent busy looping in case Run() fails quickly after Setup() succeeds

    lcdpulse.Cleanup();
  }

  return 0;
}

