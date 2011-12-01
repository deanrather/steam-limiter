#!/usr/bin/env python
#
# Copyright (C) 2011 Nigel Bree
# All Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

#-----------------------------------------------------------------------------

# This application mainly provides a simple way for installations of the Steam
# Limiter application from http://steam-limiter.googlecode.com to determine
# whether an updated version of the application has been made available. That's
# just a simple matter of retrieving some data from a URL to retrieve the
# current version number and a download URL for the latest installer.
#
# Given the way that web access to the source repository in Google Code works
# in principle that could be used instead, but there are a couple of advantages
# to having a service like this instead, in addition to the fact that as with
# the NSIS installer for the limiter client it's a handy example of how to do
# such things.
#
# For instance, an additional thing I could add to this is to have an installer
# extension for the limiter client app which can retrieve the client's real IP
# and thus suggest to it the ideal server (or server list) to set as the filter
# default, instead of assuming TelstraClear - TC is the default ISP at present
# since that's my ISP, but the filter app is usable by other New Zealand ISPs
# and it would be nice to be able to make that seamless.
#
# The tradeoff there, however, is that to learn what the external IP ranges for
# ISPs are takes some effort, and really the easiest way to do that is to log
# them (even if it's just to the Python log for me to cast an eyeball over, not
# to a datastore); similarly, it's good for the update check to not just be a
# static page.
#

import jinja2
import os
import logging
import webapp2

from google.appengine.ext.webapp import template
from google.appengine.api import users

# These will most likely eventually become some datastore items in future, but
# making them static will do just to start the update support off.

code_ui_base = 'http://code.google.com/p/steam-limiter/'
code_file_base = 'http://steam-limiter.googlecode.com/files/'

latest_version = '0.4.1.0'
latest_file = 'steamlimit-' + latest_version + '.exe'

# If I want to map source IPs to ISP names, you'd normally just do some kind of
# reverse DNS lookup: however, the API for that is blocked by Google App Engine
# and there's no reliable way to bypass it, see
#   http://code.google.com/p/googleappengine/issues/detail?id=354
#
# However, most (paid) GeoIP databases include this data too, although most are
# asking crazy prices. There's even a more-or-less free CSV database for that
# which can be obtained from MaxMind, Inc who sell the full databases, since for
# our purposes the ASN data should be all we need:
#   http://www.maxmind.com/app/asnum
# A suitable Python API for working with this is 
#   http://code.google.com/p/pygeoip/
#
# However, what I've done for simplicity for now is rather than putting the IP
# mappings into the datastore is to prepare a file with a suitable Python data
# literal I can import, such with the subset of IP ranges I care about.

import ip_match
import string

# Assist the mapping process by converting the IP address string into a number

def stringip_to_number (text):
    fields = string.split (text, '.')
    total = 0
    for item in fields:
        total = (total << 8) + int (item)

    return total;

# Find any matching tuple inside the ip_match.ip_table list, which is sorted,
# and return the ISP number for it
#
# The remapping for loopback below is to help testing; since we don't have a
# real IP to use in the local GAE dev environment, try the various known Steam
# server IPs used by different ISPs to see if we can identify their netblocks.

def find_netblock (ip):
    if ip == '127.0.0.1':
        ip = '202.124.127.66'

    if type (ip) == str:
        ip = stringip_to_number (ip)

    # Binary search or linear? The table's small enough that linear search is
    # just fine. As it happens the table is sorted so binary search will work
    # but I'm too lazy to bother given that GAE doesn't need the speed (well,
    # not given that by doing this in memory we're avoiding the datastore).

    for item in ip_match.ip_table:
    	if item [0] > ip:
            return - 1

        if item [1] > ip:
            return item [2]

    return - 1

# The ISP indexes I use in the netblock table

isps = {
    - 1: { 'name': 'Unknown', 'server': '203.167.129.4' },
    0: { 'name': 'TelstraClear New Zealand', 'server': '203.167.129.4' },
    1: { 'name': 'Orcon New Zealand', 'server': '219.88.241.90' },
    2: { 'name': 'Snap! New Zealand', 'server': '202.124.127.66' }
}

# The landing page for human readers to see

class MainHandler (webapp2.RequestHandler):
    def get (self):
        context = {
            'user': users.get_current_user ()
        }

        path = os.path.join (os.path.dirname (__file__), 'index.html')
        self.response.out.write (template.render (path, context))

# Since we're sending back data rather than plain text, abstract out the
# wrapping of the data, permitting the caller to ask for JSONP style.

import json

def send (handler, data):
    cb = handler.request.get ('cb', '()')
    if cb == '()':
        handler.response.out.write (data)
        return

    # This header already exists in the collection so adjust rather than add
    handler.response.headers ['Content-Type'] = 'application/json; charset=utf-8'

    if cb != '':
        handler.response.out.write (cb + '(')
    handler.response.out.write (json.dumps (data))
    if cb != '':
        handler.response.out.write (')')

# All the data we care about, all in a dict, for various handlers to choose
# from to render

def bundle (self):
    source = self.request.remote_addr
    netblock = find_netblock (source);

    # GAE actually includes a small amount of GeoIP itself; not what need for
    # ISP selection, but interesting nonetheless (note: only in production,
    # not in the dev server)
    # http://code.google.com/appengine/docs/python/runtime.html#Request_Headers

    country = self.request.headers.get ('X-AppEngine-Country')
    country = country or 'Unknown'

    logging.info (source + '(country=' + country + ') mapped to ' + '%x' % netblock)

    isp = isps.get (netblock);

    return {
        'latest': latest_version,
        'download': code_file_base + latest_file,
        'country': country,
        'ispname': isp ['name'],
        'filterip': isp ['server']
    }

# The query page for the latest revision, which can information about the latest
# version number in various forms

class LatestHandler (webapp2.RequestHandler):
    def get (self):
        send (self, bundle (self) ['latest'])

# A query page for redirecting to the latest download; we can choose between
# a redirect to the Google Code page for the download, or a direct link to the
# download itself.
#
# Attaching ?direct=1 to the GAE /download URL will get us to redirect to the
# file itself, otherwise we show the main download page.

class DownloadHandler (webapp2.RequestHandler):
    def get (self):
        direct = self.request.get ('direct', 'about')

        if direct == 'about':
            to = code_ui_base + 'downloads/detail?name=' + latest_file
        else:
            to = bundle (self) ['download']

        if direct != '0':
            self.redirect (to)
        else:
            send (self, to)


# A query page for exercising the IP->ISP mapping; the bit below for loopback
# is for local testing since that doesn't yield a valid IP for the matching
# algorithm to use.

class IspHandler (webapp2.RequestHandler):
    def get (self):
        send (self, bundle (self) ['ispname'])

# An alternative to the above for getting the IP value to use; here we'll
# always default to the TelstraClear one.

class FilterHandler (webapp2.RequestHandler):
    def get (self):
        send (self, bundle (self) ['filterip'])

# Return a bundle of various of the above individual pieces as a JSON-style
# map.

class BundleHandler (webapp2.RequestHandler):
    def get (self):
        send (self, bundle (self))

# Plumb up the GAE boilerplate with a mapping of URLs to handlers.

app = webapp2.WSGIApplication ([('/', MainHandler),
                                ('/latest', LatestHandler),
                                ('/download', DownloadHandler),
                                ('/ispname', IspHandler),
                                ('/filterip', FilterHandler),
                                ('/all', BundleHandler)],
                               debug = True)

def main ():
    application.run ()

if __name__ == '__main__':
    main ()
