#! /usr/bin/env python
# encoding: utf-8

# the following two variables are used by the target "waf dist"
VERSION='0.0.1'
APPNAME='lcdpulse'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
  opt.load('compiler_cxx')

def configure(conf):
  conf.load('compiler_cxx')

  conf.check(header_name='errno.h')
  conf.check(header_name='fcntl.h')
  conf.check(header_name='math.h')
  conf.check(header_name='netdb.h')
  conf.check(header_name='pulse/pulseaudio.h')
  conf.check(header_name='stdio.h')
  conf.check(header_name='string.h')
  conf.check(header_name='sys/socket.h')
  conf.check(header_name='sys/types.h')
  conf.check(header_name='unistd.h')

  conf.check(lib='m', uselib_store='m', mandatory=False)
  conf.check(lib='pulse', uselib_store='pulse')

  conf.check(function_name='clock_gettime', header_name='time.h', mandatory=False)
  conf.check(function_name='clock_gettime', header_name='time.h', lib='rt', uselib_store='rt', mandatory=False,
             msg='Checking for clock_gettime in librt')

  conf.write_config_header('config.h')

def build(bld):
  bld.program(source='src/main.cpp\
                      src/lcdpulse.cpp',
              use=['m','rt','pulse'],
              includes='./src',
              cxxflags='-Wall -g',
              target='lcdpulse')

